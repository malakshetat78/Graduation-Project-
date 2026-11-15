# ECU bring-up: build, flash and smoke-test checklist

Board: Seeed XIAO ESP32-S3 Sense (ESP32-S3R8, 8 MB flash, 8 MB octal PSRAM), OV3660 camera, L76K GPS stacked
on D6/D7, MPU6050 IMU on I2C. Firmware: ESP-IDF project `firmware/ecu/` (boot/sensor smoke test).

Every result below says where it ran: **HOST** (laptop, native tests), **EMULATOR** (Espressif QEMU), or
**BOARD** (the real XIAO). A HOST or EMULATOR pass says nothing about wiring or sensors.

## Status (2026-11-15)

| What | Where | Result | Evidence |
|---|---|---|---|
| Device logic unit tests (detection, fusion, events, signing, NMEA, queue, uplink, app loop) | HOST | **91/91 pass** | `logs/native_tests.log` |
| Malak's firmware builds for the chip (`espidf_security`, `ble`) | cross-compile | **Built**; `ble` needs the 8 MB flash setting | `STEP1_BUILD.md`, `step1_build_logs/` |
| `espidf_security` signing demo runs on the chip's CPU | EMULATOR | Signs and verifies; then **stack overflow and reboot loop** (54 reboots in 25 s). Fixed by an 8 KB main stack (no reboots) | `logs/qemu_espidf_security.log`, `logs/qemu_espidf_security_stack8k.log` |
| `firmware/ecu` smoke test builds for the board | cross-compile | **Built, 0 warnings**, 76% of app partition free | `logs/ecu_build_board.log` |
| `firmware/ecu` smoke test boots | EMULATOR | **chip PASS, signing PASS**, gps/imu/camera SKIP (not emulated), no reboots in a 60 s run | `logs/qemu_ecu_smoke.log` |
| Flash, boot, GPS, IMU, camera on the board | BOARD | **NOT RUN**: board not delivered yet | - (fill in below) |
| Wi-Fi join, signed report to the backend | BOARD | **NOT RUN**: not built yet | - |

Provenance: the ECU builds, the emulator run and the native test run were made from commit `ad1ac37` with a
clean tree (the firmware reports its version as `ad1ac37`); the logs were committed after it.
Image hashes: board `icvsp_ecu.bin` `0c7c4604ffd4fc35c5350f2dccdc8836ccd3daec54c14ec862b01be1a16cccaa`,
emulator `icvsp_ecu.bin` `50f078f0ef9b8bfc5750af6cf62b113f982ba707650abfe8832d208451b17db2`.
Malak's projects (`STEP1_BUILD.md`) were built from her commit `a452c66`.

Found and fixed while doing this:
- The chip compiler (GCC 14) rejected `format_utc`: a GNSS time after the year 9999 would have been cut off
  inside a signed report. `EventBuilder::build` now refuses such times; test
  `test_time_after_year_9999_is_refused_not_cut_off` (failed before the fix, passes after; 91/91).
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

Expected: `91 test cases: 91 succeeded`.

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

- Wi-Fi join and one signed report to the backend (laptop backend over HTTP first; Cloud Run URL not ready).
- Report delivery in its own task (the current uplink would block sensor reads while waiting on the network).
- Reports, device key and boot counter kept in flash across power loss.
- HTTPS to Cloud Run (needs the URL and the clock set before certificate checks).
- BLE signed local warning to the phone (Malak's BLE code).
- Camera AI detector (`espdet_qemu`): out of scope, model not ready.
