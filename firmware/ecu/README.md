# ECU firmware (ESP-IDF 5.5.5, XIAO ESP32-S3 Sense)

Currently the boot/sensor smoke test: `main/smoke_main.cpp` checks the chip, signing, GPS, IMU and camera, one
line each (`SMOKE <check> PASS|FAIL|SKIP ...`). Build, emulator, flash commands and the expected results are in
[`../results/bringup/BRINGUP_CHECKLIST.md`](../results/bringup/BRINGUP_CHECKLIST.md).

| File | What |
|---|---|
| `main/board_pins.h` | Every pin, in one place. IMU pins are PENDING (placeholder D4/D5). |
| `sdkconfig.defaults` | Board build: 8 MB flash, octal PSRAM, USB console, 8 KB main stack, camera on I2C port 0 |
| `sdkconfig.qemu` | Emulator overlay: UART console, quad PSRAM, hardware probes skipped |
| `components/` | Wrappers that compile `../lib/icvsp_core`, `../lib/icvsp_board` and Monocypher unchanged |
| `dependencies.lock` | Pins the camera driver (espressif/esp32-camera 2.1.9) |

Quick start:

```bash
. ~/esp/esp-idf/export.sh
idf.py set-target esp32s3 build
idf.py -p /dev/cu.usbmodem101 flash monitor
```
