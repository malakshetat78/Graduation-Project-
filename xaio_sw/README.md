# ICVSP device firmware

The device-side logic of the ICVSP unit (Basic tier: ESP32-S3 + IMU + GNSS), written so it can be built and
tested on a laptop before we have a board. Built with [PlatformIO](https://platformio.org).

| Module | What it does | Requirement |
|---|---|---|
| `lib/icvsp_core/src/bump_detector` | Detects potholes and speed bumps from the vertical acceleration | FR-27 (proposed) |
| `lib/icvsp_core/src/hazard_tracker` | Camera decision logic: confidence, road region, tracking, 3-of-5 frames, motion check for speed bumps (forward or rear camera) | FR-01 |
| `lib/icvsp_core/src/fusion` | Combines IMU and camera into one report per hazard; the IMU decides the type when they disagree | FR-27 (proposed) |
| `lib/icvsp_core/src/event_builder` | Builds the safety event, message format v2 (`engine/schema`): event ID = device-boot-sequence, `gps_valid` with null position when there is no fix, IMU jolt in g | FR-01/FR-02 |
| `lib/icvsp_core/src/device` | The whole device pipeline (detectors → fusion → event → signature) in one class, used by the demo, the system simulation and later the board | — |
| `tools/sil_vehicle.cpp` | One simulated vehicle for the system simulation (`sil/`) | — |
| `lib/icvsp_core/src/signer` | Ed25519 signature over sender, message ID, time and event | NFR-10 (proposed) |
| `lib/icvsp_core/src/replay_guard` | Rejects stale (> 30 s), future-dated and repeated messages | FR-06 |
| `lib/icvsp_sim` | Simulated IMU and GNSS: a car driving over chosen hazards | – |
| `lib/monocypher` | [Monocypher 4.0.2](https://monocypher.org) (BSD-2 / CC0), standard Ed25519 | – |
| `lib/icvsp_board/src/nmea` | GNSS NMEA parser (RMC/GGA, checksums, strict field checks, UTC carried forward, stale fix) for the NEO-6M / L76K on UART | FR-25 |
| `lib/icvsp_board/src/report_queue`, `uplink` | Offline queue of signed lines and delivery to `POST /reports` with retry; a report leaves the queue when the backend has answered for it, or when the queue is full (the oldest is dropped and counted) | proposed ECU delivery requirement |
| `lib/icvsp_board/src/wifi_link` | Wi-Fi join / reconnect state machine (driver behind an interface) | proposed ECU delivery requirement |
| `lib/icvsp_board/src/ecu_app` | The ECU main loop: waits for GNSS time, feeds fresh IMU samples to the Device, keeps delivering | FR-25, FR-SEC-05 |

`icvsp_core` and `icvsp_board` make no hardware calls (the board drivers sit behind interfaces), so the same
code runs on the laptop and on the ESP32-S3. The chip-side parts (Wi-Fi, HTTPS delivery, the device key in
flash, GPS UART, MPU6050 and camera checks) are in `ecu/`. Still to do: confirm the IMU pins, test on the board,
and feed the real IMU and GPS readings into the report firmware (it uses a simulated drive for now).

## Run it

```bash
pip install platformio cryptography
cd firmware
pio test -e native                         # unit and integration tests (106)
pio run -e native && .pio/build/native/program   # demo drive: prints signed reports as JSON
python tools/check_with_engine.py          # builds the demo, then checks its reports from the backend side
```

The first run downloads about 10 MB (PlatformIO's native platform, build tool and the Unity test framework).
Only a C++ compiler is needed (Apple clang or gcc).

`check_with_engine.py` validates every report with the trust engine's own validation and verifies the
signature with an independent Ed25519 library (Python `cryptography`), so the firmware and the backend are
known to agree on the format and the signed bytes.

## Accuracy with the decision logic

`tools/score_logic.py` runs the same tracker code over the detections saved from the two held-out Cairo videos
and scores it against the tagged hazards (results in `results/logic_accuracy.md`). On the night video the logic
cuts false speed-bump reports from 3.2 to 0.3 per minute (YOLO11n v3c) and from 3.5 to 0.7 (ESPDet-Pico), and
false pothole reports from 1.2 to 0.8. It also drops real detections: potholes found go from 4 to 2 of 6 (the two
kept are the hits confirmed by eye), and camera speed bumps from 3 to 0 of 5 (v3c) and 4 to 2 of 5 (ESPDet-Pico),
which supports the IMU as the main speed-bump sensor. The simple road region gives the same result as the
SegFormer road mask, so the chip does not need a segmentation model. Thresholds were fixed before scoring;
8 potholes and 8 bumps are too few for final conclusions.

## How the IMU detector works

The slow part of the vertical acceleration (gravity, slopes, the nose dipping when braking) is removed with a
running average; what is left is the jolt. A jolt is a hazard when it is at least 5 times the car's usual
vibration on that road and at least 2 m/s², while the car moves faster than 8 km/h. Downward first means
pothole (the wheel drops); upward first and lasting at least 0.2 s means speed bump. One report per hazard
(front and rear wheels).

**All thresholds are first guesses.** The tests use synthetic signals; the values have to be tuned on real
recordings from a phone or the board on Egyptian roads.

## Signed messages

The signature covers

    "ICVSP-v1" 0x00 sender 0x00 msg_id 0x00 utc_ms (8 bytes, big-endian) event-JSON

The event JSON is written in one fixed format, so its bytes are the canonical form. The receiver checks the
signature first and only then the replay window, so forged messages cannot fill the replay table. The
laptop demo uses a fixed public seed. The ECU firmware (`ecu/`) makes its own key at first boot from the
chip's random number generator and keeps it in flash (NVS); only the public key is printed, for registration.
Flash encryption and secure boot, which protect that key from someone holding the board, are not enabled yet.

## Known gaps

- Nothing has run on the real board yet. `ecu/` (ESP-IDF 5.5.5) builds for the XIAO ESP32-S3; in the
  emulator it boots, signs reports with its own key and delivers them over HTTP and HTTPS to the backend.
  See `testing/hardware/ecu/BRINGUP_CHECKLIST.md`.
- The report queue is RAM only: reports are lost on a reboot (flash or microSD storage is the next step).
- `road_segment` is a coarse grid cell (about 200 m) until the backend maps reports to real roads.
