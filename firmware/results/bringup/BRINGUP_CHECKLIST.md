# ECU bring-up: build, flash and smoke-test checklist

Board: Seeed XIAO ESP32-S3 Sense (ESP32-S3R8, 8 MB flash, 8 MB octal PSRAM), OV3660 camera, L76K GPS stacked
on D6/D7, MPU6050 IMU on I2C. Firmware: ESP-IDF project `firmware/ecu/` (boot/sensor smoke test).

Every result below says where it ran: **HOST** (laptop, native tests), **EMULATOR** (Espressif QEMU), or
**BOARD** (the real XIAO). A HOST or EMULATOR pass says nothing about wiring or sensors.

## Status (2026-11-16)

| What | Where | Result | Evidence |
|---|---|---|---|
| Device logic unit tests (detection, fusion, events, signing, NMEA, queue, uplink, app loop) | HOST | **106/106 pass** (at `b89fa8f`) | `logs/native_tests.log` |
| Malak's firmware builds for the chip (`espidf_security`, `ble`) | cross-compile | **Built**; `ble` needs the 8 MB flash setting | `STEP1_BUILD.md`, `step1_build_logs/` |
| `espidf_security` signing demo runs on the chip's CPU | EMULATOR | Signs and verifies; then **stack overflow and reboot loop** (54 reboots in 25 s). Fixed by an 8 KB main stack (no reboots) | `logs/qemu_espidf_security.log`, `logs/qemu_espidf_security_stack8k.log` |
| `firmware/ecu` smoke test builds for the board | cross-compile | **Built, 0 warnings**, 76% of app partition free | `logs/ecu_build_board.log` |
| `firmware/ecu` smoke test boots | EMULATOR | **chip PASS, signing PASS**, gps/imu/camera SKIP (not emulated), no reboots in a 60 s run (at `b89fa8f`) | `logs/qemu_ecu_smoke.log` |
| Flash, boot, GPS, IMU, camera on the board | BOARD | **NOT RUN**: board not delivered yet | - (fill in below) |
| Report firmware builds for the board (Wi-Fi), the emulator and the HTTPS test | cross-compile | **Built, 0 warnings**, 37% of app partition free (board) | `logs/ecu_report_build_board.log`, `logs/ecu_report_build_qemu.log`, `logs/ecu_report_build_tls.log` |
| ECU app on the chip signs a pothole report and sends it to `POST /reports` | EMULATOR + laptop backend | **PASS**: network up, SNTP time, report signed, backend answered **201 accepted**, no reboots. QEMU Ethernet stands in for Wi-Fi; HTTP here (HTTPS in the next row); IMU drive and GNSS sentences simulated; the chip's own key, made at first boot (`DEMO key created`) and loaded from flash on the next boot (`DEMO key loaded`) | `logs/qemu_report_demo.log`, `logs/backend_report_demo.log`, screenshot 5. The run before registering the key got **403 unknown_device**: the ECU kept the report and retried it with back-off (`not_provisioned=4 kept=1`, nothing deleted): `logs/qemu_report_demo_unregistered.log`, screenshot 7 |
| HTTPS: certificate checks | EMULATOR + laptop backend | **5/5 PASS**: untrusted CA, wrong host name and expired certificate refused (mbedTLS -0x2700, report kept, backend received nothing); valid certificate accepted (403 before registration, then 201). Test CA only, not the hosted backend | `logs/tls/summary.txt`, `logs/tls/` (section 4c) |
| Backend checks on that report | HOST | Stored as a pothole, severity high; the same line again = duplicate (safe retry); one field changed = **401 bad signature** | `logs/backend_report_checks.log` (made by `backend_report_checks.py`), screenshot 6 |
| Wi-Fi join, signed report to the backend | BOARD | **NOT RUN**: board not delivered yet (same firmware, `sdkconfig.report` without the QEMU overlay) | - |

Provenance: all five firmware images (smoke test board and emulator, report board, emulator and HTTPS test)
were built from scratch from commit `b89fa8f` with a clean tree (every image reports its version as `b89fa8f`),
and the native tests, the emulator smoke test, the report runs and the HTTPS checks were run from those images.
The logs were committed afterwards. Image hashes (`b89fa8f`, also in `logs/image_hashes.txt`):
smoke board `icvsp_ecu.bin` `acb4f84b4ed9f71968d98b6de339cf319e456c96d3a6ac7d9b9363543d5d9dec`,
smoke emulator `icvsp_ecu.bin` `082ff731a3a561082eaae8b95515f180bd2cfe6049c65c3b63fe308c7f8c7acf`,
report board `icvsp_ecu.bin` `fb348d6c9851d5eea264075d0038c9e8533f28cf4b8480fc5ab1eb8e3b660559`,
report emulator `flash_qemu.bin` `e003598ad39df6c3207f0163249c609f790a0fc24a08d5135c9ada7cdd3da333`,
HTTPS test `flash_qemu.bin` `34a38ea313c6ead8b7b6350e61a0b7990fab61395436292acbdd7fef6068d79f`
(the HTTPS image embeds the test CA made on this laptop, and `sdkconfig.local` can differ, so another laptop
gets different hashes for the report images).
Malak's projects (`STEP1_BUILD.md`) were built from her commit `a452c66`.

Found and fixed while doing this:
- The chip compiler (GCC 14) rejected `format_utc`: a GNSS time after the year 9999 would have been cut off
  inside a signed report. `EventBuilder::build` now refuses such times; test
  `test_time_after_year_9999_is_refused_not_cut_off` (failed before the fix, passes after).
- Sending used to happen inside the ECU loop, so a slow network would have delayed the IMU reads. Delivery now
  runs in its own task; test `test_app_loop_never_touches_the_network`.
- The retry delay after a failed send started when the send began; with a 10 s HTTP timeout the whole delay was
  already used up and the next try followed at once. It now starts when the attempt ends; test
  `test_retry_delay_counts_from_the_end_of_a_slow_attempt` (failed before the fix). Found by Codex review, with
  two more fixed in the same commit: a lost IP address was not noticed, and a malformed demo seed was accepted.
- A second Codex review: any 200/201 deleted the report, so a captive portal or a wrong URL answering 200 could
  lose reports. Now only the backend's ACK for that report counts (`"ack":"stored"` and its event ID); test
  `test_only_a_stored_ack_for_this_report_counts` (failed before the fix). Also: the demo result now counts
  reports pushed out of a full queue, start-up memory failures stop with a message, and section 4b below was
  made copy-paste runnable (it was not: the token placeholder broke the shell command).
- A third review: that ACK check searched the text for exact strings, so a normal JSON reply with spaces
  would have been rejected and an error object containing those words accepted. Replies are now read as JSON
  (`json_reply.cpp`): only top-level `ack`, `event_id` (= the report's `msg_id`) and `device_id` (= its sender)
  count; truncated, malformed or nested-only replies are not an ACK. Also, a 403 `unknown_device` (key not
  registered yet, normal for a new board) used to delete the report; now it is kept and retried with back-off
  and the ECU reports `not_provisioned`. Other 403s (revoked device) and 400/401/409/413/422 still drop it.
  Tests `test_json_reader_handles_escapes_and_rejects_bad_json`, `test_unknown_device_keeps_the_report_until_registered`,
  `test_other_403_and_signature_refusals_stay_final`, `test_success_status_without_a_valid_ack_is_retried`
  (they did not compile or failed before the change).
- A fourth review (Codex) of that JSON reader: an escaped `\u0000` could cut `"stored\u0000junk"` down to
  `"stored"`, raw bytes were not checked to be valid UTF-8, key names over 31 bytes could not match, and the ACK
  buffers were smaller than the IDs the backend allows. All fixed; tests
  `test_json_reader_rejects_nul_and_bad_utf8_and_reads_long_keys`, `test_ack_with_the_longest_ids_the_backend_allows_counts`.
- ESP-IDF's default build does not check certificate dates (`CONFIG_MBEDTLS_HAVE_TIME_DATE` off), so an
  expired server certificate would have been accepted. Now on in `sdkconfig.report`; HTTPS case 3 shows the
  refusal.
- A Codex re-check of the per-device key: the usual ESP-IDF start-up code erases NVS when it reports no free
  pages or a new format, which would have replaced the signing key silently. The firmware now stops with a
  message instead.
- The camera driver takes its I2C port from the build config, not from the code; pinned to port 0 so the IMU
  can use port 1 (`sdkconfig.defaults`).
- QEMU has no octal PSRAM: the emulator build uses quad mode (`sdkconfig.qemu`); the board build stays octal.

## 1. Toolchain (once per laptop)

| Tool | Version used |
|---|---|
| ESP-IDF | v5.5.5, installed in `~/esp/esp-idf` with `./install.sh esp32s3` |
| Compiler | xtensa-esp-elf-gcc 14.2.0 (comes with ESP-IDF) |
| QEMU | Espressif qemu-xtensa 9.2.2 (`python ~/esp/esp-idf/tools/idf_tools.py install qemu-xtensa`, then open a new terminal and run `export.sh` again so it is on the PATH; on Linux it also needs `libgcrypt20 libglib2.0-0 libpixman-1-0 libsdl2-2.0-0 libslirp0`) |
| PlatformIO | 6.2.0 (host tests only) |
| Host | macOS 26.6.2 arm64 |

In every new terminal:

```bash
. ~/esp/esp-idf/export.sh
```

## 2. Host tests

```bash
cd firmware
pio test -e native
```

Expected: `106 test cases: 106 succeeded`.

## 3. Build for the board

```bash
cd firmware/ecu
idf.py set-target esp32s3 build
```

Expected: `Project build complete`, no warnings. The camera driver version is pinned in `dependencies.lock`.

## 4. Run in the emulator

```bash
cd firmware/ecu
idf.py -B build_qemu -D SDKCONFIG=build_qemu/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu" set-target esp32s3 build
cd build_qemu
python -m esptool --chip esp32s3 merge_bin --fill-flash-size 8MB -o flash_qemu.bin @flash_args
qemu-system-xtensa -nographic -machine esp32s3 -m 8M -drive file=flash_qemu.bin,if=mtd,format=raw
```

(Quit with Ctrl-A then X.) Expected lines:

```
SMOKE chip     PASS model=ESP32-S3 cores=2 ... flash=8192 KB psram=8192 KB ...
SMOKE signing  PASS sign=ok verify=ok tampered_rejected=yes ...
SMOKE gps      SKIP no GPS receiver in the emulator
SMOKE imu      SKIP no I2C sensors in the emulator
SMOKE camera   SKIP no camera in the emulator
SMOKE RESULT pass=2 fail=0 skip=3 (emulator: hardware checks skipped)
```

In the emulator the chip, flash and PSRAM values come from QEMU's model, and timings are not chip timings.

## 4b. Signed report demo in the emulator (network)

Terminal 1, the backend on the laptop with a fresh throw-away database (never the team's Supabase). The admin
token is made up here and saved in a file so Terminal 3 can use it:

```bash
cd backend
python3 -m venv .venv && .venv/bin/pip install -r requirements.txt
rm -f /tmp/icvsp_demo.sqlite3
python3 -c "import secrets; print(secrets.token_hex(16))" > /tmp/icvsp_demo_admin_token
env -u DATABASE_URL -u SUPABASE_URL -u SUPABASE_JWT_SECRET ICVSP_DB=/tmp/icvsp_demo.sqlite3 ICVSP_ALLOW_ANONYMOUS=1 \
  ICVSP_ADMIN_TOKEN="$(cat /tmp/icvsp_demo_admin_token)" .venv/bin/uvicorn --factory icvsp_backend.app:create_app --port 8010
```

Terminal 2, the firmware (each ECU makes its own signing key at first boot; nothing to set for it):

```bash
cd firmware/ecu
cp sdkconfig.local.example sdkconfig.local     # Wi-Fi name/password for the board; nothing needed for QEMU
idf.py -B build_report_qemu -D SDKCONFIG=build_report_qemu/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu;sdkconfig.report;sdkconfig.report_qemu;sdkconfig.local" \
  set-target esp32s3 build
cd build_report_qemu
python -m esptool --chip esp32s3 merge_bin --fill-flash-size 8MB -o flash_qemu.bin @flash_args
qemu-system-xtensa -nographic -machine esp32s3 -m 8M -drive file=flash_qemu.bin,if=mtd,format=raw -nic user,model=open_eth
```

The first boot prints `DEMO key created (per-device key, NVS)` and `DEMO device=ecu-<MAC> ... public_key=...` (64 hex
characters; in QEMU the MAC is blank, so the ID is `ecu-000000000000`). The backend does not know the key
yet and answers 403 `unknown_device`; the ECU keeps the report, retries it with back-off and prints
`DEMO RESULT FAIL ... not_provisioned=N kept=1 ... - register this device's public key with the backend`. Register it once (Terminal 3), pasting the
ID and key it printed:

```bash
ID=PASTE_DEVICE_ID_HERE
PK=PASTE_KEY_HERE
curl -X POST localhost:8010/devices -H "X-Admin-Token: $(cat /tmp/icvsp_demo_admin_token)" \
  -H 'Content-Type: application/json' -d "{\"device_id\":\"$ID\",\"public_key\":\"$PK\",\"vetted\":false}"
```

then quit QEMU (Ctrl-A then X) and start it again (same flash file, so `DEMO key loaded`: the same key). Expected: `NET link=online`, `DEMO time synced by SNTP`, `REPORT signed #1`,
`HTTP POST ... -> 201 {"status":"accepted"...}`, `DEMO RESULT PASS signed=1 stored_by_backend=1`. SNTP needs the
laptop to have internet. 10.0.2.2 is the laptop as seen from inside QEMU.

The backend checks in `logs/backend_report_checks.log` (stored view, resend = duplicate, one field changed = 401)
were made after the run, with the backend still up, by:

```bash
python3 firmware/results/bringup/backend_report_checks.py /tmp/icvsp_demo.sqlite3
```

It reads the stored line from the test database, posts it again unchanged, then posts it with
`"severity":"high"` changed to `"severity":"low"`.

**On the board** (when it arrives). In `sdkconfig.local` set `CONFIG_ICVSP_WIFI_SSID`, `CONFIG_ICVSP_WIFI_PASSWORD`
and `CONFIG_ICVSP_REPORT_URL="http://LAPTOP_LAN_IP:8010/reports"` (the laptop's address on the same Wi-Fi, from
`ipconfig getifaddr en0`). Start the backend as above but add `--host 0.0.0.0` so the board can reach it. Then
build and flash the report firmware from its own build folder (the plain `idf.py flash` of section 5 flashes the
smoke test instead):

```bash
cd firmware/ecu
idf.py -B build_report_board -D SDKCONFIG=build_report_board/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.report;sdkconfig.local" set-target esp32s3 build
idf.py -B build_report_board -p /dev/cu.usbmodem101 flash monitor
```

Expected: `NET link=online (wifi)`, then the same lines as in the emulator. Register the board's key the same way.

## 4c. HTTPS checks in the emulator (certificates)

The report firmware sends over HTTPS when its URL starts with `https://`. On a real deployment it checks the
server certificate against ESP-IDF's bundle of public root certificates (the hosted backend, planned on AWS). For a test on the laptop,
a separate build trusts **only** a throw-away test CA, and the laptop backend serves HTTPS with a different
certificate in each case. Certificate dates are checked (`CONFIG_MBEDTLS_HAVE_TIME_DATE=y`, now in
`sdkconfig.report`; it is off by default in ESP-IDF, which would accept an expired certificate).
The report firmware speaks **TLS 1.3 only** (as FR-SEC-08 asks of the phone/app link): `CONFIG_MBEDTLS_SSL_PROTO_TLS1_3=y` in `sdkconfig.report`
and `CONFIG_ICVSP_TLS_REQUIRE_1_3` (default on) sets `tls_version = ESP_HTTP_CLIENT_TLS_VER_TLS_1_3` in
`main/http_transport.cpp`, so it never falls back to TLS 1.2. AWS Lambda function URLs accept TLS 1.3 (checked
from the laptop with `openssl s_client -tls1_3` against a `*.lambda-url.us-east-1.on.aws` host).

```bash
cd firmware/ecu
../../backend/.venv/bin/python ../results/bringup/tls/make_test_certs.py tls_test   # test CA + 4 server certs (git-ignored)
idf.py -B build_report_tls -D SDKCONFIG=build_report_tls/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu;sdkconfig.report;sdkconfig.report_qemu;sdkconfig.report_tls;sdkconfig.local" \
  set-target esp32s3 build
(cd build_report_tls && python -m esptool --chip esp32s3 merge_bin --fill-flash-size 8MB -o flash_qemu.bin @flash_args)
../results/bringup/tls/run_tls_checks.sh ../results/bringup/logs/tls   # about 9 minutes
```

| Case | Server certificate | Expected on the ECU | Result |
|---|---|---|---|
| 1 | from a CA the firmware does not know | TLS refused: `HTTP POST ... -> no answer`, report kept, backend receives nothing | PASS |
| 2 | test CA, but for `wrong-host.example` | the same | PASS |
| 3 | test CA, right address, valid 2020-2021 only (expired) | the same | PASS |
| 4 | test CA, right address, valid; device not registered | TLS accepted: backend answers `403 unknown_device`, report kept | PASS |
| 5 | the same, after registering the device's key over HTTPS | TLS accepted: `201`, `DEMO RESULT PASS` | PASS |
| 6 | the same good certificate, device registered, but the server allows only TLS 1.2 | TLS refused (no fallback to 1.2): `-> no answer`, report kept, nothing stored | PASS |

For cases 1-3 the script also checks that the firmware log shows a certificate-verification failure
(mbedTLS `-0x2700`), and confirms from the laptop (Python's TLS client, same CA) that each certificate fails for
exactly its intended reason; the firmware itself cannot print the reason (mbedTLS drops the certificate on failure).
The laptop check also prints the TLS version the server agrees to: `TLSv1.3` in cases 4-5, `TLSv1.2` in case 6.
Case 6 would get `201` if the firmware fell back to TLS 1.2, since the device is registered by then; instead the
server closes the handshake (mbedTLS `-0x7280`, connection closed) because the firmware offers only TLS 1.3.
Logs: `logs/tls/` (one serial log per case, the backend log and `summary.txt`). Still open: a run against the real hosted backend (AWS)
URL (public root bundle).

## Requirements to tests (V-model, ECU part)

What each ECU requirement is checked by, at which level, and the result so far. IDs are from
`verification/requirements.csv`; "proposed" rows are ECU behaviour that has no requirement ID yet. HOST = unit
tests on the laptop (`pio test -e native`, listed in `verification/traceability.csv`), EMULATOR = the firmware
in QEMU, BUILD = cross-compile for the chip, BOARD = the real XIAO ESP32-S3 (not delivered yet).

| Requirement | What is checked | Level | Test / evidence | Result |
|---|---|---|---|---|
| Task 1: firmware builds for the ESP32-S3 | smoke test and report firmware compile with 0 warnings | BUILD | `logs/ecu_build_board.log`, `logs/ecu_report_build_board.log` | PASS |
| Pins (board design) | no GPIO used twice; no strapping/USB/flash/PSRAM pin | BUILD | `static_assert` in `ecu/main/board_pins.h`; `PIN_MAP.md` | PASS (build); wiring NOT RUN |
| FR-01 detect potholes, output a confidence | bump detection, camera/IMU fusion, event building | HOST | F-01..F-38 (`test_bump_detector`, `test_event_builder`, `test_fusion`, `test_hazard_tracker`) | PASS |
| FR-01 on the real sensors | camera captures a frame; IMU answers and reads 1 g | BOARD | smoke test checks 5 and 6 (section 6) | NOT RUN |
| FR-25 GNSS time and position | NMEA parsing, checksums, fix and UTC handling | HOST | F-55..F-73 (`test_nmea`) | PASS |
| FR-25 on the real L76K | NMEA bytes and sentences arrive on UART1; fix outdoors | BOARD | smoke test checks 4 and 8 | NOT RUN |
| FR-SEC-03 unique device identity with an Ed25519 key | key made on the chip at first boot, kept in flash, different per board, never silently replaced (NVS is never auto-erased); ID from the MAC | HOST + EMULATOR | F-101..F-106 (`test_device_identity`); `logs/qemu_report_demo_unregistered.log` (`DEMO key created`), `logs/qemu_report_demo.log` (`DEMO key loaded`, same public key) | PASS (host, emulator); BOARD NOT RUN |
| FR-SEC-04 controlled provisioning | unregistered key gets 403 and the report is kept; after `POST /devices` the same key is accepted | EMULATOR | `logs/qemu_report_demo_unregistered.log`, `logs/qemu_report_demo.log`, F-96 | PASS |
| FR-SEC-05 sign on the device | Ed25519 (RFC 8032 vectors), signed bytes forwarded unchanged; backend verifies the chip's report | HOST + EMULATOR | F-46..F-52 (`test_signer`), `test_event_builder`; backend 201, changed copy 401 (`logs/backend_report_checks.log`) | PASS |
| FR-SEC-06 canonical signed fields | fixed field order and format | HOST | F-11, F-48 | PASS |
| FR-SEC-09 unique message ID and signed send time | device ID + boot ID + sequence | HOST | F-12, F-14 | PASS |
| FR-SEC-10 reject replayed or stale messages (device side) | seen-ID and freshness checks | HOST | F-39..F-45 (`test_replay_guard`) | PASS |
| FR-SEC-08 HTTPS/TLS transport (ECU to backend, mentor architecture) | certificate chain, host name and dates checked; bad certificates refused, report kept; TLS 1.3 only, no fallback to 1.2 | EMULATOR | section 4c, `logs/tls/` | PASS (6/6, test CA) |
| FR-SEC-08 against the hosted backend (AWS) | public root bundle, real URL | EMULATOR / BOARD | - | NOT RUN (URL not ready) |
| (proposed) ECU delivery: no report lost | only the backend's ACK for that report deletes it (JSON read strictly); retries with back-off; 403 unknown_device kept; offline queue | HOST + EMULATOR | F-74..F-82, F-93..F-100 (`test_uplink`); F-86..F-90, F-92 (`test_ecu_app`) | PASS |
| (proposed) network link recovery | join, lost IP, re-join, give-up and retry | HOST (fake driver) | F-83..F-85 (`test_wifi_link`) | PASS; real Wi-Fi radio NOT RUN |
| NFR-SEC-02 no hard-coded private keys | per-device key by default; demo seed only in git-ignored `sdkconfig.local` | inspection + HOST | `ecu/main/Kconfig.projbuild`, F-101..F-106 | PASS |
| NFR-SEC-05 crypto overhead on the target | `sign_ms` on the real chip | BOARD | smoke test check 3 | NOT RUN |
| SEC-EXT-E stored key vs physical access | flash encryption + NVS encryption | BOARD | - | NOT RUN (key is in plain NVS for now) |
| Wi-Fi join on the radio | `NET link=online (wifi)` and a signed report from the board | BOARD | section 4b "On the board" | NOT RUN |

## 5. Flash the board

Before power: fit the Wi-Fi/BT antenna, seat the camera ribbon, check the GPS module is stacked the right way,
wire the MPU6050 (VCC 3V3, GND, SDA, SCL to the pins in `ecu/main/board_pins.h`, **pending Malak**).

```bash
ls /dev/cu.usbmodem*
cd firmware/ecu
idf.py -p /dev/cu.usbmodem101 flash monitor
```

Use the port name `ls` printed (`/dev/cu.usbmodem101` is an example). If no port appears or flashing fails:
hold BOOT, tap RESET, release BOOT, flash again, then tap RESET. Quit the monitor with Ctrl-].

Save the log for the evidence:

```bash
idf.py -p /dev/cu.usbmodem101 monitor | tee ../results/bringup/logs/board_smoke_$(date +%Y%m%d_%H%M).log
```

## 6. Board smoke test: expected results

Fill in after the run. Result: PASS / FAIL / NOT RUN.

| # | Check | Expected | If it fails | Result | Log |
|---|---|---|---|---|---|
| 1 | Boot | `SMOKE BEGIN ... BOARD build`, no `Rebooting` | Check USB cable/port; read the error above `abort()` | NOT RUN | |
| 2 | chip | ESP32-S3, 2 cores, 8192 KB flash, 8192 KB PSRAM | PSRAM 0: wrong board variant or PSRAM mode | NOT RUN | |
| 3 | signing | sign/verify ok, tampered rejected; note `sign_ms` (real chip timing) | Report to Shahesta: core library issue | NOT RUN | |
| 4 | gps | `good_sentences` > 0 within 5 s, `checksum_errors` near 0; `fix=yes` only outdoors after a few minutes | 0 bytes: module not seated or TX/RX swapped (swap `GPS_TX_GPIO`/`GPS_RX_GPIO`); bytes but no sentences: wrong baud | NOT RUN | |
| 5 | imu | address 0x68 (or 0x69), `WHO_AM_I=0x68`, mean about 1.000 g lying still | Nothing at 0x68/0x69: wiring or pins; other WHO_AM_I: a clone or a different chip | NOT RUN | |
| 6 | camera | `PID=0x3660`, frame captured, 320x240, `JPEG markers=ok` (start and end markers present; the image is not decoded, so this proves capture, not image quality) | Init failed: ribbon not seated; other PID: different sensor | NOT RUN | |
| 7 | LED | Blinks slowly (1 s) if nothing failed, fast if something failed | - | NOT RUN | |
| 8 | GPS outdoors (optional) | Leave the board running outdoors with the monitor open: a `SMOKE gps-live` line every 10 s, then `SMOKE gps-fix first fix N s after boot` once (the first fix is caught even if it lasts only a moment) and `fix=yes`, `sats` >= 4, `utc_known=yes` (a cold start can take several minutes) | No fix after 15 min: antenna needs open sky | NOT RUN | |

After the summary the GPS keeps being read: a `SMOKE gps-live` line every 10 s (counts, satellites, fix yes/no).
The log never contains the GPS position or camera pixels; only counts, sizes and IDs.

## 7. Not covered yet (planned, after the smoke test)

- Wi-Fi join and a signed report from the real board (done in the emulator over emulated Ethernet; board not
  delivered yet).
- Reports and boot counter kept in flash across power loss (the device key already is: NVS).
- HTTPS to the hosted backend (AWS) with the public root bundle (the certificate checks are tested against a local test CA in
  section 4c, TLS 1.3 only; AWS URL not ready).
- Flash encryption and NVS encryption, so the stored key cannot be read out of the flash.
- Clock from GNSS on the road; the report demo uses SNTP in place of the GNSS clock.
- Wi-Fi reconnect behaviour on the real radio (tested only against the fake driver in the host tests).
- BLE signed local warning to the phone (Malak's BLE code).
- Camera AI detector (`espdet_qemu`): out of scope, model not ready.
