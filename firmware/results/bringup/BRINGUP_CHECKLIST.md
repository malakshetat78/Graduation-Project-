# ECU bring-up: build, flash and smoke-test checklist

Board: Seeed XIAO ESP32-S3 Sense (ESP32-S3R8, 8 MB flash, 8 MB octal PSRAM), OV3660 camera, L76K GPS stacked
on D6/D7, MPU6050 IMU on I2C. Firmware: ESP-IDF project `firmware/ecu/` (boot/sensor smoke test).

Every result below says where it ran: **HOST** (laptop, native tests), **EMULATOR** (Espressif QEMU), or
**BOARD** (the real XIAO). A HOST or EMULATOR pass says nothing about wiring or sensors.

## Status (2026-11-15)

| What | Where | Result | Evidence |
|---|---|---|---|
| Device logic unit tests (detection, fusion, events, signing, NMEA, queue, uplink, app loop) | HOST | **93/93 pass** (at `3e704d3`) | `logs/native_tests.log` |
| Malak's firmware builds for the chip (`espidf_security`, `ble`) | cross-compile | **Built**; `ble` needs the 8 MB flash setting | `STEP1_BUILD.md`, `step1_build_logs/` |
| `espidf_security` signing demo runs on the chip's CPU | EMULATOR | Signs and verifies; then **stack overflow and reboot loop** (54 reboots in 25 s). Fixed by an 8 KB main stack (no reboots) | `logs/qemu_espidf_security.log`, `logs/qemu_espidf_security_stack8k.log` |
| `firmware/ecu` smoke test builds for the board | cross-compile | **Built, 0 warnings**, 76% of app partition free | `logs/ecu_build_board.log` |
| `firmware/ecu` smoke test boots | EMULATOR | **chip PASS, signing PASS**, gps/imu/camera SKIP (not emulated), no reboots in a 60 s run | `logs/qemu_ecu_smoke.log` |
| Flash, boot, GPS, IMU, camera on the board | BOARD | **NOT RUN**: board not delivered yet | - (fill in below) |
| Report demo firmware builds for the board (Wi-Fi) and the emulator | cross-compile | **Built, 0 warnings**, 37% of app partition free (board) | `logs/ecu_report_build_board.log`, `logs/ecu_report_build_qemu.log` |
| ECU app on the chip signs a pothole report and sends it to `POST /reports` | EMULATOR + laptop backend | **PASS**: network up, SNTP time, report signed, backend answered **201 accepted**, no reboots. QEMU Ethernet stands in for Wi-Fi; HTTP only (TLS not exercised); IMU drive and GNSS sentences simulated; demo key | `logs/qemu_report_demo.log`, `logs/backend_report_demo.log`, screenshot 5 |
| Backend checks on that report | HOST | Stored as a pothole, severity high; the same line again = duplicate (safe retry); one field changed = **401 bad signature** | `logs/backend_report_checks.log`, screenshot 6 |
| Wi-Fi join, signed report to the backend | BOARD | **NOT RUN**: board not delivered yet (same firmware, `sdkconfig.report` without the QEMU overlay) | - |

Provenance: the smoke-test builds and emulator run were made from commit `ad1ac37` with a clean tree (the
firmware reports its version as `ad1ac37`); the report demo builds, the report emulator run and the latest native
test run from commit `3e704d3`, also clean (the firmware reports `3e704d3`). Logs were committed after each.
Report demo image hashes: board `1a90dad96d47ce98f3dcbe35c346127cac40e5a9b1a8bfae90fd232a3693ecec`, emulator
`be527d8481cf52c2d06f58b0599107d94d63c29671bdc5e697dca55f2df0ae09` (the emulator image includes the local demo
seed from `sdkconfig.local`, so another laptop gets a different hash).
Smoke test image hashes (`ad1ac37`):
board `icvsp_ecu.bin` `0c7c4604ffd4fc35c5350f2dccdc8836ccd3daec54c14ec862b01be1a16cccaa`,
emulator `icvsp_ecu.bin` `50f078f0ef9b8bfc5750af6cf62b113f982ba707650abfe8832d208451b17db2`.
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

Expected: `93 test cases: 93 succeeded`.

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

Terminal 2, a demo key and the firmware:

```bash
cd firmware/ecu
cp sdkconfig.local.example sdkconfig.local     # put a fresh seed in CONFIG_ICVSP_DEMO_SEED_HEX (command inside)
idf.py -B build_report_qemu -D SDKCONFIG=build_report_qemu/sdkconfig \
  -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.qemu;sdkconfig.report;sdkconfig.report_qemu;sdkconfig.local" \
  set-target esp32s3 build
cd build_report_qemu
python -m esptool --chip esp32s3 merge_bin --fill-flash-size 8MB -o flash_qemu.bin @flash_args
qemu-system-xtensa -nographic -machine esp32s3 -m 8M -drive file=flash_qemu.bin,if=mtd,format=raw -nic user,model=open_eth
```

The first boot prints `DEMO device=ecu-demo-01 ... public_key=...` (64 hex characters) and its report is refused
(403 unknown device), because the backend does not know the key yet. Register it once (Terminal 3), pasting the
key in place of PASTE_KEY_HERE:

```bash
PK=PASTE_KEY_HERE
curl -X POST localhost:8010/devices -H "X-Admin-Token: $(cat /tmp/icvsp_demo_admin_token)" \
  -H 'Content-Type: application/json' -d "{\"device_id\":\"ecu-demo-01\",\"public_key\":\"$PK\",\"vetted\":false}"
```

then quit QEMU (Ctrl-A then X) and start it again. Expected: `NET link=online`, `DEMO time synced by SNTP`, `REPORT signed #1`,
`HTTP POST ... -> 201 {"status":"accepted"...}`, `DEMO RESULT PASS signed=1 stored_by_backend=1`. SNTP needs the
laptop to have internet. 10.0.2.2 is the laptop as seen from inside QEMU.

The backend checks in `logs/backend_report_checks.log` (stored view, resend = duplicate, one field changed = 401)
were made with a short Python script after the run: it read the stored line from the test database, posted it
again unchanged, then posted it with `"severity":"high"` changed to `"severity":"low"`. The commands and answers
are in that log.

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
- Reports, device key and boot counter kept in flash across power loss.
- HTTPS to Cloud Run: the code checks https:// servers against ESP-IDF's root certificate bundle, but no TLS run
  has been made (Cloud Run URL not ready).
- Clock from GNSS on the road; the report demo uses SNTP in place of the GNSS clock.
- Wi-Fi reconnect behaviour on the real radio (tested only against the fake driver in the host tests).
- BLE signed local warning to the phone (Malak's BLE code).
- Camera AI detector (`espdet_qemu`): out of scope, model not ready.
