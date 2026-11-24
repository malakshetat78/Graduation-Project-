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
      gnss_app.cpp     T-014 GNSS bring-up (Ahmed): L76K fix, staleness, disconnect and recovery
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

Only the chosen program runs on the board; the others are still compiled but never started.
`#define ICVSP_DEBUG 1` / `0` turns the extra progress and per-reading lines on or off for every program.

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

The test cases and the result tables to fill in are in the team repo: `testing/hardware/camera/T-012-camera-test-cases.md`.

The IMU (Rana) and BLE (Mariam) programs are added to this sketch in their own pull requests.

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
