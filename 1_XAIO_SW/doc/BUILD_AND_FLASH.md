# ECU firmware: build, flash, bring up and run

Everything you need to go from a fresh clone to a running ECU. The project is in `1_XAIO_SW/code` (ESP-IDF,
nothing Arduino). The longer reference (folder layout, each test firmware, common errors) is
[README.md](README.md).

## The one line

In an ESP-IDF terminal, in `1_XAIO_SW/code`:

```bash
python build.py prod flash monitor -p <PORT>
```

This builds the production firmware, flashes it and opens the serial monitor. `<PORT>` is the board's port, which
depends on your computer (table below). Leave out `flash monitor -p ...` to only build.
`python build.py` alone lists every build type. `python build.py all` builds them all, which is a quick check
that everything compiles.

| | Windows | macOS | Linux |
|---|---|---|---|
| Port (`<PORT>`) | `COM5` (any `COMx`) | `/dev/cu.usbmodem1101` (any `/dev/cu.usbmodem*`) | `/dev/ttyACM0` (any `/dev/ttyACM*`) |
| Find the port | Device Manager > Ports (COM & LPT) | `ls /dev/cu.usbmodem*` | `ls /dev/ttyACM*` |
| VS Code command palette | `Ctrl+Shift+P` | `Cmd+Shift+P` | `Ctrl+Shift+P` |
| First time only | - | - | `sudo usermod -aG dialout $USER`, then log out and in (permission to use the port) |

The port is also shown when you click the plug icon in VS Code's bottom bar. Everything else in this guide is the
same on all three.

## 1. What it runs on

| | |
|---|---|
| Board | Seeed Studio **XIAO ESP32-S3 Sense**: ESP32-S3 (2 cores, 240 MHz), 8 MB flash, 8 MB PSRAM, Wi-Fi and Bluetooth LE |
| Camera | OV3660 on the Sense board |
| GNSS | Seeed L76K (UART, pads D6/D7), with its antenna |
| IMU | MPU6050 (I2C, pads D4/D5) |
| Phone | the ROVO app (`2_FLUTTER_SW`) over Bluetooth LE: shows the warning and forwards each signed report |
| Backend | FastAPI on AWS Lambda with the Supabase database (`3_WEB_SW`), over Wi-Fi and HTTPS (optional on the ECU) |
| Power | USB-C, 5 V (a laptop on the bench, a 5 V USB car adapter in the car) |
| Toolchain | **ESP-IDF v5.5** (tested with v5.5.5), VS Code with the ESP-IDF extension, Python 3. Windows, macOS or Linux |

Wiring and pins: [README.md, section 4](README.md#4-wiring). Every pin is defined only in `main/board_pins.h`.

## 2. Build types

| Type | Command | What it is | Goes in the car? |
|---|---|---|---|
| **Production** | `python build.py prod` | The all-in-one ECU: IMU + GNSS + camera + BLE (+ Wi-Fi) running together. Smaller, faster code (-Os). After a crash it prints the reason and restarts. Each ECU uses its own signing key made on the chip; the demo key cannot be selected | **Yes** (once it passes its board tests, see below) |
| **Development** | `python build.py dev` | The same firmware with debug checks: debug code (-Og), stops on a crash and keeps the backtrace on screen, stack and heap corruption checks | No, bench only |
| **Test** | `python build.py smoke`, `camera`, `imu`, `gnss`, `ble`, `report` | One part at a time. These produce the board test results in `5_TESTING/doc` (T-012 camera, T-010 IMU, T-014 GNSS, T-016 BLE). `smoke` checks the whole board in one go. `report` sends a signed report to the backend over Wi-Fi | No |

The ECU prints its build type at start-up, for example
`ECU BEGIN all-in-one firmware <version> (production build), ...`. The version comes from `git describe`, so every
binary can be traced back to its commit.

How the types are made: each one is a stack of small settings files, defined in `CMakePresets.json` (the same list
VS Code shows):

```
production  = sdkconfig.defaults + sdkconfig.ecu + sdkconfig.prod + sdkconfig.local
development = sdkconfig.defaults + sdkconfig.ecu + sdkconfig.dev  + sdkconfig.local
test        = sdkconfig.defaults + sdkconfig.<part>
```

- `sdkconfig.defaults` holds the board settings: 8 MB flash, PSRAM, USB console.
- `sdkconfig.ecu` sets up the all-in-one firmware: Wi-Fi and Bluetooth together, a 1 ms scheduler tick, TLS 1.3.
- `sdkconfig.local` is your own Wi-Fi name, password and backend URL. It is git-ignored and optional: without it the
  ECU sends reports to the phone only.

**Status, said plainly:** each test firmware has been run on the physical board (results in `5_TESTING/doc`).
The production firmware builds with 0 warnings and was code-reviewed, but it **has not yet run on the board**. Its
test cases are in [5_TESTING/doc/ecu_all_in_one](../../5_TESTING/doc/ecu_all_in_one/ECU-all-in-one-test-cases.md).
It should not go in a car until TC-ECU-01 to TC-ECU-10 pass.

## 3. First time: install and clone (once per computer)

1. Install **VS Code**, then the **ESP-IDF** extension by Espressif Systems (Extensions, search "ESP-IDF").
2. The extension opens the **ESP-IDF Installation Manager (EIM)**. Install **v5.5**. Do not install 6.x: this
   project does not build with it. If 6.x is already installed, add v5.5 next to it.
3. In VS Code open the command palette (`Ctrl+Shift+P`, macOS `Cmd+Shift+P`) > **ESP-IDF: Select Current ESP-IDF Version** > **v5.5**.
4. Clone and open the firmware folder:
   ```bash
   git clone https://github.com/malakshetat78/Graduation-Project-.git
   ```
   Then in VS Code go to **File > Open Folder** > `Graduation-Project-/1_XAIO_SW/code`.
5. Command palette > **ESP-IDF: Open ESP-IDF Terminal**. Every `python build.py ...` command runs in this
   terminal.
6. Optional, only for Wi-Fi to the backend: copy `sdkconfig.local.example` to `sdkconfig.local` and fill it in.
   Never commit it.

## 4. Build

```bash
python build.py prod
```

The first build takes a few minutes, because it downloads the camera driver and compiles ESP-IDF. Later builds take
seconds. The output ends with the **memory table** and `Project build complete`. The firmware file is
`build_prod/icvsp_ecu.bin`.

From VS Code instead: command palette > **ESP-IDF: Select Project Configuration** > "PRODUCTION: ...", then click
the **Build** (wrench) icon in the bottom bar.

## 5. Flash

1. Plug the XIAO in with a **data** USB-C cable.
2. Find the port (table at the top): `COM5` on Windows, `/dev/cu.usbmodem...` on macOS, `/dev/ttyACM0` on Linux.
3. Flash:
   ```bash
   python build.py prod flash -p <PORT>
   ```

What gets written:

| Address | File | What |
|---|---|---|
| `0x0` | `bootloader/bootloader.bin` | ESP-IDF bootloader |
| `0x8000` | `partition_table/partition-table.bin` | partition table: `nvs` 24 KB at `0x9000`, `phy_init` at `0xf000`, app 1.5 MB at `0x10000` |
| `0x10000` | `icvsp_ecu.bin` | the firmware |

Flashing does **not** touch `nvs`. That area holds the ECU's signing key, the IMU calibration and the BLE counter,
so they survive every re-flash. **Never run "Erase flash"** (`idf.py erase-flash`): it deletes the key, and the ECU
gets a new identity that the backend does not know.

If flashing cannot connect, put the board in download mode: hold **B** (boot), press and release **R** (reset),
let go of **B**, then flash again. Afterwards press **R** once to start the new firmware.

## 6. Start and watch it

```bash
python build.py prod monitor -p <PORT>          # Ctrl+] to leave
```

The ECU starts by itself about 2 seconds after it gets power. Nothing has to be pressed. What it prints:

```
ECU BEGIN all-in-one firmware <version> (production build), ESP-IDF v5.5.5: IMU + GNSS + camera + BLE ...
ECU device=ecu-<MAC> boot_id=<n> key=loaded public_key=<64 hex>
IMU DETECTED: MPU6050 at 0x68, ... 100 Hz, ... calibration loaded
GNSS UART1 ready ...
CAM started (QVGA, one photo every 5000 ms)
BLE advertising ...
ECU t=5s | IMU ok 100/s ... | GNSS ... fix=no time=no ... | app=waiting-for-gnss-time | CAM ok=1 ... | BLE ... | RAM ...
```

- A **status line every 5 seconds** shows every part: IMU rate, GNSS fix and time, camera photos, BLE link, Wi-Fi,
  signed reports, free RAM and stack.
- Coordinates are never printed.
- Until the GNSS has a fix with UTC time (outdoors, 1 to 5 minutes), the app waits (`app=waiting-for-gnss-time`).
  It then sets the clock and shows `app=running`.

## 7. Bring-up on a new board

Do these in order the first time a board is assembled. Each step uses a test build, and each has full test cases in
`5_TESTING/doc`.

| Step | Command | Pass when you see | Test cases |
|---|---|---|---|
| 1. Board check | `python build.py smoke flash monitor -p <PORT>` | `SMOKE RESULT pass=5 fail=0` | - |
| 2. Camera | `python build.py camera flash monitor -p <PORT>` | `CAM RESULT ... fail=0`, photos look right | [T-012](../../5_TESTING/doc/camera/T-012-camera-test-cases.md) |
| 3. IMU, and its calibration (stored in NVS) | `python build.py imu flash monitor -p <PORT>` | `IMU DETECTED`, about 1 g at rest, calibration saved | [T-010](../../5_TESTING/doc/imu/T-010-imu-test-cases.md) |
| 4. GNSS (outdoors) | `python build.py gnss flash monitor -p <PORT>` | fix quality > 0 and UTC time within 5 minutes | [T-014](../../5_TESTING/doc/gnss/T-014-gnss-test-cases.md) |
| 5. BLE to the phone | `python build.py ble flash monitor -p <PORT>` | the ROVO app connects and ACKs the signed reports | [T-016](../../5_TESTING/doc/ble/T-016-ble-test-cases.md) |
| 6. Everything together | `python build.py prod flash monitor -p <PORT>` | status line every 5 s, IMU about 100/s, no restarts for 10 min | [TC-ECU-01 to 10](../../5_TESTING/doc/ecu_all_in_one/ECU-all-in-one-test-cases.md) |

## 8. In the car

1. Mount the board flat, with the IMU's X arrow pointing to the front of the car. If the Y arrow points forward
   instead, change **All-in-one ECU: how the IMU sits in the car** in menuconfig
   (`python build.py prod menuconfig`) and flash again.
2. Put the GNSS antenna where it sees the sky, for example under the windscreen.
3. Power it from a 5 V USB car adapter. The ECU starts by itself, and the ROVO app on the driver's phone connects
   over Bluetooth.
4. Reports are made only while driving: the detector ignores jolts below **8 km/h** (GNSS speed), and nothing is
   signed before the GNSS gives the time. Each signed report goes to the phone and, if Wi-Fi is set up, to the
   backend.

## 9. Not done yet before real production

- **Board tests of the production build** (TC-ECU-01 to TC-ECU-10) and tuning the detector thresholds on real
  drives.
- **Secure boot and flash encryption** are not turned on. Turning them on burns eFuses in the chip, which is
  permanent, so it is a planned, deliberate step and is not in this build.
- **Updates over the air (OTA):** for now, updating means flashing over USB.
- Reports waiting for the phone are kept in RAM only, so they are lost if power is cut.
- The BLE link has no pairing or owner check yet.
