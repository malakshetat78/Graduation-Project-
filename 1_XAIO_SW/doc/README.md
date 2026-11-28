# ICVSP ECU firmware (ESP-IDF, VS Code)

One ESP-IDF project for the Seeed **XIAO ESP32-S3 Sense**, in `1_XAIO_SW/code`. Every part of the ECU lives
there, and you pick which firmware to build from a list in VS Code. There is no Arduino code.

- ESP-IDF **v5.5** (tested with v5.5.5). Everyone must use the same version: v6.x will not build this project.
- Target chip: **esp32s3**.

## 1. Folder layout

```
1_XAIO_SW/code/
  CMakeLists.txt          project file
  CMakePresets.json       the firmware list shown in VS Code
  sdkconfig.defaults      board settings shared by every firmware (8 MB flash, PSRAM, USB console)
  sdkconfig.<name>        one small file per firmware (camera, imu, gnss, ble, report, ecu)
  sdkconfig.local.example copy to sdkconfig.local for Wi-Fi name/password (never commit it)
  dependencies.lock       pins the camera driver version (espressif/esp32-camera)
  main/
    smoke_main.cpp        smoke test: chip, signing, GNSS, IMU, camera (one line each)
    camera_main.cpp       T-012 camera test
    imu_main.cpp          T-010 IMU test
    gnss_main.cpp         T-014 GNSS test
    ble_main.cpp          T-016 BLE test
    report_demo_main.cpp  signed report to the backend over Wi-Fi + HTTPS
    ecu_main.cpp          all-in-one ECU: every part together (not tested on the board yet)
    board_pins.h          every pin, in one place
    net_link.*, http_transport.*, nvs_secret_store.*, sim.h   used by the report firmware
  components/
    icvsp_camera/         camera (OV3660)
    icvsp_imu/            IMU (MPU6050 / clones)
    icvsp_gnss/           GNSS (L76K)
    icvsp_ble/            BLE to the phone (NimBLE)
    icvsp_core/           detection, events, Ed25519 signing (no hardware calls)
    icvsp_board/          NMEA parser, report queue, uplink, device ID (no hardware calls)
    icvsp_monocypher/     Monocypher (Ed25519), third party
1_XAIO_SW/doc/README.md   this file
5_TESTING/doc/<part>/     test cases (T-0xx) and the logs of every board run
5_TESTING/code/<part>/    laptop tools and tests (camera photo extractor, IMU calibration, NMEA parser)
```

| Firmware (VS Code list) | Ticket | Owner | Main file | Component |
|---|---|---|---|---|
| Smoke test (board check) | - | Shahesta | `smoke_main.cpp` | all |
| T-012 camera test | T-012 | Shahesta | `camera_main.cpp` | `icvsp_camera` |
| T-010 IMU test | T-010 | Rana | `imu_main.cpp` | `icvsp_imu` |
| T-014 GNSS test | T-014 | Ahmed | `gnss_main.cpp` | `icvsp_gnss` |
| T-016 BLE test | T-016 | Mariam | `ble_main.cpp` | `icvsp_ble` |
| Signed report to the backend over Wi-Fi | - | Shahesta | `report_demo_main.cpp` | `icvsp_core`, `icvsp_board` |
| All-in-one ECU (not tested yet) | - | Shahesta | `ecu_main.cpp` | all |

## 2. Install (once)

### Windows and Mac

1. Install **VS Code**: https://code.visualstudio.com
2. In VS Code open **Extensions** (the four squares on the left), search **ESP-IDF** and install the one by
   **Espressif Systems**.
3. The extension asks for the **ESP-IDF Installation Manager (EIM)**. Let it download EIM, then in EIM choose
   **version v5.5** (not 6.x). This downloads about 2 GB.
4. Back in VS Code: `Ctrl+Shift+P` (Mac: `Cmd+Shift+P`) and run **ESP-IDF: Select Current ESP-IDF Version**,
   pick **v5.5**.

Extra for Windows: if the board does not show a COM port, install the USB driver from Windows Update
(the XIAO ESP32-S3 uses the chip's built-in USB, no CP210x driver is needed).

## 3. Open, build, flash, monitor

1. **File > Open Folder** and open `1_XAIO_SW/code` (this folder, not the repo root).
2. `Ctrl/Cmd+Shift+P` > **ESP-IDF: Set Espressif Device Target** > **esp32s3** >
   "ESP32-S3 chip (via builtin USB-JTAG)".
3. `Ctrl/Cmd+Shift+P` > **ESP-IDF: Select Project Configuration** > pick the firmware from the table above.
4. Plug in the XIAO with a **data** USB-C cable. In the blue bar at the bottom, click the plug icon and pick
   the port: Mac `/dev/cu.usbmodem...`, Windows `COMx`.
5. Click the **flame icon (Build, Flash and Monitor)** in the bottom bar. The first build takes a few minutes.
6. To stop the monitor: `Ctrl+]`.

Each firmware builds in its own folder (`build`, `build_camera`, `build_imu`, ...), so switching between them
does not rebuild everything.

Same thing from a terminal (after running ESP-IDF's `export.sh` / `export.ps1`):

```bash
idf.py --preset camera build                       # names: smoke, camera, imu, gnss, ble, report, ecu
idf.py --preset camera -p /dev/cu.usbmodem1101 flash monitor
```

If your `idf.py` has no `--preset`, use the folder and files from `CMakePresets.json` directly, for example:
`idf.py -B build_camera -D SDKCONFIG=build_camera/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.camera" build`.

### Board stuck or no port?

Hold **B** (boot), press and release **R** (reset), let go of **B**, then flash again. After flashing press
**R** once so the new firmware starts.

## 4. Wiring

All pins are in [`main/board_pins.h`](../code/main/board_pins.h). Change them only there.

| Part | Module | XIAO pad | GPIO | Notes |
|---|---|---|---|---|
| Camera | OV3660 on the Sense board | fixed | 10, 11-18, 38-40, 47, 48 | nothing to wire; the camera ribbon must be clicked in |
| GNSS | Seeed L76K | D6 = ESP TX -> L76K RX, D7 = ESP RX <- L76K TX | 43, 44 | 9600 baud; antenna needs open sky |
| IMU | MPU6050 (clones 0x70/0x72/0x98 accepted) | D4 = SDA, D5 = SCL | 5, 6 | 3V3 and GND; I2C port 1 (port 0 is the camera) |
| BLE | built into the chip | - | - | clip the antenna onto the U.FL connector, or the link drops |
| LED | on-board yellow LED | - | 21 | low = on |

## 5. What each firmware does and how to test it

Each test's full test cases, steps and results are in `5_TESTING/doc/<part>/`.

| Firmware | Prints | Test cases |
|---|---|---|
| Smoke test | `SMOKE <check> PASS/FAIL ...` then `SMOKE RESULT pass=5 fail=0` | - |
| Camera | `CAM ...` lines, photos as Base64; `CAM RESULT ... fail=0` | [T-012](../../5_TESTING/doc/camera/T-012-camera-test-cases.md); turn the log into photos with `5_TESTING/code/camera/extract_jpegs.py` |
| IMU | `IMU DETECTED`, readings every 200 ms, `LOST` / `BACK` when a wire is pulled, calibration | [T-010](../../5_TESTING/doc/imu/T-010-imu-test-cases.md) |
| GNSS | fix quality, satellites, HDOP every second (coordinates hidden by default: they are private) | [T-014](../../5_TESTING/doc/gnss/T-014-gnss-test-cases.md) |
| BLE | `BLE advertising as ICVSP-001`, signed reports sent in fragments, ACKs from the phone | [T-016](../../5_TESTING/doc/ble/T-016-ble-test-cases.md); phone side: nRF Connect or the ROVO app |
| Report | joins Wi-Fi, gets the time, signs a report, `POST /reports` over HTTPS | needs `sdkconfig.local` (below) |
| All-in-one ECU (**not tested on the board yet**) | every part together: IMU + GNSS + camera + BLE (+ Wi-Fi), status line every 5 s | [ECU_ALL_IN_ONE.md](ECU_ALL_IN_ONE.md) |

Settings for each test (number of photos, light label, BLE name, ...) are in
**ESP-IDF: SDK Configuration Editor (menuconfig)**: search the setting, change it, **Save**, build again.

### Report firmware: Wi-Fi and backend

```bash
cp sdkconfig.local.example sdkconfig.local     # then put your Wi-Fi name, password and the backend URL in it
```

`sdkconfig.local` is git-ignored. Never commit it and never paste it in a chat.

The signing key is made on the chip at the first start and kept in flash (NVS). **Do not run "Erase flash"**:
it deletes the key, the IMU calibration and the BLE counter, and the board gets a new identity.

## 6. Laptop tests (no board)

These check the code around the hardware on a computer (macOS or Linux, C++17 compiler, Python 3).
They are not hardware evidence.

```bash
5_TESTING/code/camera/run_host_tests.sh      # JPEG check, Base64, photo extractor
5_TESTING/code/gnss/run_nmea_tests.sh        # NMEA parser (23 tests)
(cd 5_TESTING/code/device_logic && pio test -e native)   # all 110 device-logic tests (needs: pip install platformio)
c++ -std=c++17 -I 1_XAIO_SW/code/components/icvsp_imu 5_TESTING/code/imu/test_accel_cal.cpp \
    1_XAIO_SW/code/components/icvsp_imu/accel_cal.cpp -o /tmp/t && /tmp/t     # IMU calibration
```

## 7. Common errors

| Error | Fix |
|---|---|
| `idf.py` or `cmake` not found | ESP-IDF is not selected: run **ESP-IDF: Select Current ESP-IDF Version** > v5.5 |
| Build errors right after a v6.x install | wrong version; install v5.5 in EIM and select it |
| No port / `Failed to connect to ESP32-S3` | use a data cable; boot mode: hold B, tap R, release B |
| Monitor shows nothing after flashing | press **R** on the board |
| Camera: `camera not supported` on the first start | press R; the test retries 3 times by itself |
| IMU `not found` | check SDA on D4 and SCL on D5, 3V3 and GND; then press R |
| GNSS: data but no fix | go outside with open sky and wait 1-5 minutes |
| BLE keeps disconnecting (`reason=0x208`) | antenna not clipped on; keep the phone close |
| Phone cannot see `ICVSP-001` | another phone/app is still connected (only one at a time); close it |
| `sdkconfig.local` missing (report firmware) | copy `sdkconfig.local.example` to `sdkconfig.local` |
