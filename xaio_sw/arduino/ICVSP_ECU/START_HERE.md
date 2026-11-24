# ICVSP ECU in the Arduino IDE: start here

## What is in this folder

```
ICVSP_ECU/
  ICVSP_ECU.ino      THE main file: setup() and loop(). It runs what config.h picks
  config.h           settings: which program (smoke test, report firmware or camera test), AWS URL, position
  secrets.example.h  copy it to secrets.h, then put your phone hotspot name and password in secrets.h
  secrets.h          (you make it) never send or commit this file: git ignores it
  Makefile           optional: build from Terminal instead of the IDE
  src/
    app/             the programs ICVSP_ECU.ino starts
      smoke_test.cpp   hardware check: chip, PSRAM, signing, GPS, IMU, camera
      report_app.cpp   report firmware: Wi-Fi -> time -> key -> signed report -> AWS
      camera_test.cpp  T-012 camera bring-up: photos at 3 sizes, timing, memory, failures, recovery
      imu_app.cpp      MPU6050 IMU test (Rana): readings, IMU LOST / IMU BACK
      ble_app.cpp      BLE GATT server (Mariam): sends simulated reports to the phone
      net_link.cpp     Wi-Fi join / reconnect
      http_transport.cpp   HTTPS POST to AWS
      nvs_secret_store.cpp keeps the board's signing key in flash
      log.cpp          prints to the Serial Monitor
    core/            road-event logic: bump detection, fusion, report JSON, signing (same as the team repo)
    board/           ECU loop, GPS (NMEA) parser, report queue, Wi-Fi state machine, uplink retries
    crypto/          Monocypher (Ed25519 signatures)
    sim/             the simulated 30 s drive with one pothole (used by the report firmware)

Camera log tool: testing/hardware/camera/tools/extract_jpegs.py turns a saved camera test output into .jpg photos and
result tables (runs on the Mac).
```

How the files work together: when you press Verify or Upload, the Arduino IDE compiles **every** `.cpp` and `.c`
file in this folder and in `src/` and links them into one program. They find each other through the
`#include "..."` lines at the top of each file. `ICVSP_ECU.ino` is where the board starts: Arduino calls `setup()`
once after reset, then `loop()` forever. You only ever change `config.h` (and `secrets.h` for Wi-Fi).

## One-time setup in the Arduino IDE

0. In this folder, copy `secrets.example.h` to `secrets.h` (Terminal: `cp secrets.example.h secrets.h`).
   Every program needs the file to exist; only the report firmware uses what is in it.
1. File > Open > this folder > `ICVSP_ECU.ino`. The IDE shows `ICVSP_ECU.ino`, `config.h` and `secrets.h` as tabs.
   (The `src/` files are compiled too, even though they have no tab.)
2. Tools > Board > esp32 > **XIAO_ESP32S3**
3. Tools > Port > **/dev/cu.usbmodem101**
4. Tools > PSRAM > **OPI PSRAM**
5. Tools > Erase All Flash Before Sketch Upload > **Disabled** (leave it Disabled always: Enabled deletes the board's key)
6. Tools > USB CDC On Boot > **Enabled**

## Pick the program to test (one line in config.h)

Everyone uses this same sketch. To test only your part, change **one line** in `config.h`, then Verify and Upload:

| You want to test | Line in `config.h` |
|---|---|
| Whole-board smoke test | `#define ICVSP_APP ICVSP_APP_SMOKE` |
| Signed reports to AWS (needs hotspot) | `#define ICVSP_APP ICVSP_APP_REPORT` |
| Camera (T-012) | `#define ICVSP_APP ICVSP_APP_CAMERA` |
| IMU / MPU6050 (Rana) | `#define ICVSP_APP ICVSP_APP_IMU` |
| BLE to the phone (Mariam) | `#define ICVSP_APP ICVSP_APP_BLE` |

Only the chosen program runs on the board; the others are still compiled but never started.
`#define ICVSP_DEBUG 1` / `0` turns the extra progress and per-reading lines on or off for every program.
The BLE program needs the **NimBLE-Arduino** library (Tools > Manage Libraries > search NimBLE-Arduino >
install version 2.5.2 by h2zero). Only the BLE program needs it: the other programs compile without it.

## Step 1: smoke test (no Wi-Fi needed)

1. In `config.h` the line must say: `#define ICVSP_APP ICVSP_APP_SMOKE`
2. Click **Verify** (the check mark). Wait for "Done compiling". This only builds; nothing goes to the board.
3. Click **Upload** (the arrow). Wait for "Hard resetting via RTS pin".
4. Tools > Serial Monitor, set **115200 baud**. Press the board's **RESET** button once to see it from the start.
5. Leave the board lying flat and still for the 10 seconds it takes.

Good result:

```
SMOKE BEGIN ICVSP ECU smoke test (Arduino build)
SMOKE chip    PASS  ESP32-S3 ...
SMOKE psram   PASS  8192 KB
SMOKE signing PASS  sign=ok verify=ok tampered_rejected=yes ...
SMOKE gps     PASS  ... good NMEA ... fix=no (normal indoors)
SMOKE imu     PASS  addr=0x68 WHO_AM_I=0x68 |a|=1.00 g ...
SMOKE camera  PASS  sensor PID=0x3660 (OV3660), frame 320x240 ...
SMOKE RESULT pass=6 fail=0
```

The LED blinks slowly when everything passed, fast when something failed. A FAIL line says what to check.
Take a screenshot of the Serial Monitor for the checklist.

## Step 2: report firmware, first boot (needs the phone hotspot)

1. Turn on the phone hotspot: 2.4 GHz, WPA2 (iPhone: Maximize Compatibility ON). Keep its screen open.
2. In `secrets.h`, put the hotspot name and password between the quotes. Exactly, same capitals.
3. In `config.h` change the line to: `#define ICVSP_APP ICVSP_APP_REPORT`
4. Verify, then Upload, then Serial Monitor at 115200, then press RESET once.
5. Wait about 1.5 minutes. Expected, in this order:

```
DEMO BEGIN ...
NET link=online (wifi) ...
DEMO time synced by SNTP ...
DEMO key created (per-device key, NVS)
DEMO device=ecu-xxxxxxxxxxxx boot_id=... public_key=<64 hex characters>
REPORT signed #1 ...
HTTP POST https://... -> 403 ... unknown_device ...
DEMO RESULT FAIL ... kept=1 ... - register this device's public key with the backend
```

The 403 is **expected**: AWS does not know this board yet.

## Step 3: send the device ID and public key to Nour

Copy the `device=ecu-...` value and the `public_key=...` value from the `DEMO device=` line and send both to
Nour so she can register the board. Only the public key: it is safe to share. Wait until she says done.

## Step 4: after Nour registered the board

1. Do **not** upload again and do not erase. Hotspot on.
2. Open the Serial Monitor and press RESET once.
3. Expected: `DEMO key loaded` (the same key as step 2), `HTTP POST ... -> 201`, `DEMO RESULT PASS`.
   Take a screenshot.

## Step 5 (optional): Wi-Fi loss test

1. Serial Monitor open, press RESET.
2. As soon as you see `DEMO time synced`, turn the hotspot OFF.
3. Wait for `REPORT signed #1` (about 15-20 s later).
4. Turn the hotspot back ON. Wait for `NET link=online (wifi)` and then `HTTP POST ... -> 201`.
   (`DEMO RESULT FAIL ... kept=1` before that is fine if the 201 comes after it.)
   Do not press RESET during this test: unsent reports are kept in RAM only.

## Camera bring-up test (T-012, no Wi-Fi needed)

Part A, without the board (Verify only):

1. In `config.h` the line must say: `#define ICVSP_APP ICVSP_APP_CAMERA`
2. Optional: set `CONFIG_ICVSP_CAM_LIGHTING` to the light you will test in, e.g. `"bright room"`.
3. Click **Verify**. Wait for "Done compiling". That proves the code builds. Nothing goes to a board and nothing
   is tested yet.

Part B, with the board (camera ribbon connected while the USB cable is **unplugged**):

1. Plug in the board, click **Upload**, open the Serial Monitor at 115200, press **RESET** once.
2. Keep the board still, pointed at the same scene. The test takes about one minute and ends with
   `CAM RESULT pass=13 fail=0` (slow LED blink). Fast blink = a FAIL line says what went wrong.
3. Check the `CAM sensor` line: it should say OV3660, PID=0x3660.
4. Copy the whole Serial Monitor output (click in it, Cmd+A, Cmd+C) into a text file, e.g. `run1_bright.txt`.
5. Get the photos and result tables on the Mac (Terminal, in this folder):

   ```
   python3 ../../../testing/hardware/camera/tools/extract_jpegs.py run1_bright.txt --out run1_bright
   ```

   The last line is the verdict: "one complete run, all checks passed, all photos OK" is what we want. Install
   Pillow once (`python3 -m pip install Pillow`) so every photo is also decoded. Open the .jpg files anyway: they
   must show the scene. Put the printed tables in the test record. Save one run per text file.
6. Repeat with `"dim room"` and `"lens covered"` in `CONFIG_ICVSP_CAM_LIGHTING` (Upload again after each change).
7. Missing-camera test: unplug USB, take the camera ribbon off, plug in, RESET. Expected: 3 start attempts,
   `CAM startup FAIL` with an error code, no crash, fast blink. Unplug USB, put the ribbon back.

The test cases and the result tables to fill in are in the team repo: `5_TESTING/doc/T-012-camera-test-cases.md`.

## IMU test (Rana)

1. **USB unplugged.** Wire the MPU6050: SDA -> D4, SCL -> D5, VCC -> 3V3, GND -> GND.
2. `config.h`: `#define ICVSP_APP ICVSP_APP_IMU`. Verify, Upload, Serial Monitor 115200, RESET.
3. Keep it still for the first second (gyroscope calibration). Expected:
   `IMU found at 0x68, WHO_AM_I=0x68 (MPU6050)`, then `IMU DETECTED`, then one line every 200 ms:
   `IMU #12 ax=+0.01 ay=-0.02 az=+1.00 g |a|=1.00 g  gx=+0.1 gy=-0.2 gz=+0.0 deg/s`.
   If WHO_AM_I is not 0x68 the line says so: write the value down (it is a compatible clone).
4. Checks for the test record: lying flat |a| about 1.00 g; each of the 6 sides: which axis is +1 or -1 g;
   rotate it by hand: which gyro axis moves; pull the SDA wire: `IMU LOST`; put it back: `IMU BACK`.
   Repeat 3 times and save the Serial Monitor output each time.

## BLE test (Mariam)

1. Library NimBLE-Arduino 2.5.2 installed (see "Pick the program").
2. `config.h`: `#define ICVSP_APP ICVSP_APP_BLE`. Verify, Upload, Serial Monitor 115200, RESET.
   Expected: `BLE advertising as ICVSP-001, address ...`.
3. Phone with Wi-Fi and mobile data **off**. With nRF Connect (or the Flutter app built from Malak's BLE branch):
   scan, connect to ICVSP-001, enable notifications on 7b100002-...: the Serial Monitor shows
   `BLE central subscribed`, `BLE new report transport_id=1`, then `BLE notify ... fragment=1/4 ...`.
4. Write `0001` (hex) to 7b100003-...: `BLE ACK accepted transport_id=1`, then report 2 starts.
5. Write a wrong ID (`0063`) and a 1-byte value (`01`): `BLE WARN ...ignored` lines, nothing else changes.
6. Do not ACK: after 2 s `sending transport_id=.. again (attempt 2/3)`, then attempt 3, then `stalled`.
7. Disconnect on the phone: `BLE central disconnected ... advertising again`. Reconnect and subscribe:
   if the report was already ACKed, a new report starts with a new transport ID; if it was not ACKed yet,
   the same unacknowledged report resumes with the same transport ID, restarting from fragment 0.
8. Record: phone model and Android/iOS version, nRF Connect or app version and commit, this sketch's commit,
   NimBLE-Arduino 2.5.2, Arduino-ESP32 core 3.3.12. Security: none (no pairing, any phone can connect).

## Rules

- Never set "Erase All Flash Before Sketch Upload" to Enabled: it deletes the board's key and Nour would have
  to register it again. Uploading normally keeps the key.
- Never send `secrets.h` or a built `.bin` file to anyone: they contain the hotspot password.
- If Upload cannot find the board: hold BOOT, tap RESET, release BOOT, then Upload again.
- If the Serial Monitor shows nothing: press RESET once while it is open.

## Differences from the ESP-IDF build in the team repo

The Arduino ESP32 core (3.3.12) is built by Espressif with fixed settings that we cannot change from the IDE:

- **TLS 1.2 instead of TLS 1.3** to AWS. TLS 1.3 is switched off in the core. The requirement FR-SEC-08
  (TLS 1.3) is only met by the ESP-IDF build (`firmware/ecu`), so say "TLS 1.2 (Arduino build)" in any results.
- **Certificate dates are not checked** (the certificate chain itself is still checked against the public root
  certificates).
- If the flash storage ever reports a new format, the Arduino core erases it on its own before `setup()` runs
  (the ESP-IDF build stops instead). That would make a new key: register it again with Nour.

Everything else (detection, signing, report format, Wi-Fi reconnect, retries, AWS checks) is the same code as
the team repo (`firmware/lib` and `firmware/ecu/main`).