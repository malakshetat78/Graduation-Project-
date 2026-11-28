# All-in-one ECU firmware (NOT YET TESTED ON THE BOARD)

One firmware where every part of the ECU runs at the same time: IMU, GNSS, camera, BLE to the phone and, if set
up, Wi-Fi to the backend. In VS Code pick **"All-in-one ECU (IMU, GNSS, camera, BLE, Wi-Fi)"** (preset `ecu`).
Main file: `1_XAIO_SW/code/main/ecu_main.cpp`.

**Status:** builds with ESP-IDF v5.5.5 with 0 warnings and was reviewed twice (Codex). It has **not been run on
the board yet**. Each part was tested alone (T-010, T-012, T-014, T-016); running them together is what the
test cases in [5_TESTING/doc/ecu_all_in_one](../../5_TESTING/doc/ecu_all_in_one/ECU-all-in-one-test-cases.md)
must show.

## How the parts work together

```
L76K GNSS (UART, D6/D7) --NMEA--> parser: UTC time, position, speed --\
MPU6050 IMU (I2C, D4/D5) --100 Hz--------------------------------------> EcuApp -> detect pothole/speed bump
                                                                            -> add place and time -> sign (Ed25519)
                                                     signed report --+--> BLE -> phone app (local warning, ACK)
                                                                     +--> Wi-Fi -> backend POST /reports (HTTPS)
OV3660 camera: one photo every 5 s, checked as a JPEG (runs alongside; no hazard detection from photos yet)
```

| Task | Core | What |
|---|---|---|
| ecu_app | 1 | every 5 ms: GNSS bytes, IMU sample (100 Hz, only new ones), detection, signing, status line every 5 s |
| BLE (NimBLE) | 1 | phone link, same GATT service and ACK rules as T-016 |
| delivery | 0 | only with a Wi-Fi name in `sdkconfig.local`: Wi-Fi link and upload, oldest report first |
| camera | 0 | one QVGA photo every 5 s (`ICVSP_ECU_CAMERA_PERIOD_MS`), restart after 3 bad photos |

## Rules it follows

- **Nothing is simulated.** Time and position come only from GNSS. Until the L76K gives a UTC time, no report is
  made (status `app=waiting-for-gnss-time`). The pothole detector ignores jolts below **8 km/h** (GNSS speed), so
  **reports need a real drive**; tapping the board on a desk does not make one.
- **Coordinates are never printed** (the status line shows only `fix=yes/no`).
- The same signed report goes to the phone and to the backend, byte for byte.
- The system clock is set from GNSS before any HTTPS upload (the certificate dates need a real clock).
- After the IMU is lost or has a gap over 100 ms, the detector starts again, so old and new samples are not mixed.
- The key, IMU calibration and boot counter are read from NVS, which is never erased. The IMU calibration made
  with the T-010 firmware (type C there) is used here.

## The status line (every 5 s)

```
ECU t=..s | IMU ok 100/s used=../s gaps=.. clipped=.. lost=.. | GNSS bytes=.. sentences=.. sats=.. quality=..
fix=yes/no time=yes/no speed=..km/h | app=running | CAM ok=.. fail=.. restarts=.. | BLE subscribed acked=..
waiting=.. dropped=.. | WIFI online stored=.. ... | signed=.. | RAM internal=.. min=.. block=.. psram=..
| stack app=.. cam=.. net=.. | worst_loop=..us
```

`IMU 100/s` with `gaps` staying low shows the IMU keeps its rate while everything else runs. `RAM min` must stay
well above 0 and `stack` values above ~500 bytes.

## Settings

- IMU mounting (menuconfig, **All-in-one ECU: how the IMU sits in the car**): flat with the sensor's X arrow
  forward (default) or Y arrow forward.
- Camera period: **All-in-one ECU: milliseconds between camera photos**.
- Wi-Fi: `sdkconfig.local` (copy `sdkconfig.local.example`). Empty Wi-Fi name: phone only.

## Known limits

- No hazard detection from the camera on the chip yet (needs the AI model on the ESP32-S3).
- Reports waiting for the phone or Wi-Fi are kept in RAM only: lost on a reboot.
- Detector thresholds are first guesses from simulated drives; real drives are needed to tune them.
- The BLE link has no pairing or owner check yet (as in T-016).
