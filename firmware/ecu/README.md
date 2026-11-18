# ECU firmware (ESP-IDF 5.5.5, XIAO ESP32-S3 Sense)

Two firmwares from one project (Kconfig `ICVSP_APP`):

- **Smoke test** (default): `main/smoke_main.cpp` checks the chip, signing, GPS, IMU and camera, one line each
  (`SMOKE <check> PASS|FAIL|SKIP ...`).
- **Report demo** (`sdkconfig.report`): `main/report_demo_main.cpp` runs the ECU app on the chip and sends each
  signed report to the backend's `POST /reports` from a separate delivery task (Wi-Fi on the board, QEMU's
  emulated Ethernet in the emulator). The IMU drive and GNSS sentences are simulated and say so in the log.
 Build, emulator, flash commands and the expected results are in
[`../results/bringup/BRINGUP_CHECKLIST.md`](../results/bringup/BRINGUP_CHECKLIST.md).

| File | What |
|---|---|
| `main/board_pins.h` | Every pin, in one place. IMU on D4/D5 (confirmed by Malak's design); stacked GPS on D6/D7 plus D0/D2. |
| `sdkconfig.defaults` | Board build: 8 MB flash, octal PSRAM, USB console, 8 KB main stack, camera on I2C port 0 |
| `sdkconfig.qemu` | Emulator overlay: UART console, quad PSRAM, hardware probes skipped |
| `sdkconfig.report` | Selects the report demo firmware |
| `sdkconfig.report_qemu` | Report demo in QEMU: emulated Ethernet instead of Wi-Fi |
| `sdkconfig.report_tls` | Report demo in QEMU over HTTPS with a test CA (certificate checks, checklist section 4c) |
| `sdkconfig.local.example` | Copy to `sdkconfig.local` (git-ignored): Wi-Fi name/password, backend URL |
| `main/net_link.*` | Network link (Wi-Fi station / QEMU Ethernet) behind the tested `WifiLink` |
| `main/nvs_secret_store.*` | Keeps the device's signing key in flash (NVS); made at first boot, loaded on every later boot |
| `main/http_transport.*` | `POST /reports` with esp_http_client (root certificate bundle for https) behind the tested `Uplink` |
| `components/` | Wrappers that compile `../lib/icvsp_core`, `../lib/icvsp_board` and Monocypher unchanged |
| `dependencies.lock` | Pins the camera driver (espressif/esp32-camera 2.1.9) |

Quick start:

```bash
. ~/esp/esp-idf/export.sh
idf.py set-target esp32s3 build
idf.py -p /dev/cu.usbmodem101 flash monitor
```
