# Rovo: Trust-Aware Cooperative Vehicle Safety Platform (ICVSP)

Graduation project, 2026-2027. A low-cost unit added to any car that detects road hazards (potholes, speed bumps),
checks them with on-board sensors and with reports from other cars, and warns drivers who are approaching them. It
keeps working when the network or the cloud is down.

## The parts

| Folder | What it is |
|---|---|
| [`1_XAIO_SW`](1_XAIO_SW/) | **ECU firmware** (ESP-IDF, XIAO ESP32-S3 Sense): camera, IMU, GNSS, BLE to the phone, signed reports |
| [`2_FLUTTER_SW`](2_FLUTTER_SW/) | **ROVO phone app** (Flutter): BLE link to the ECU, warnings, map; forwards each signed report |
| [`3_WEB_SW`](3_WEB_SW/) | **Trust and consensus engine** (decides which hazards are real, with a written reason) and the **backend** (FastAPI on AWS Lambda, Supabase database, operator dashboard) |
| [`4_AI_SW`](4_AI_SW/) | **Camera hazard detectors**: YOLO11n for a Pro unit, ESPDet-Pico for the ESP32-S3 |
| [`5_TESTING`](5_TESTING/) | Board test cases with the log of every run, laptop tests, and the requirement-to-test traceability check |
| [`6_TOOLS`](6_TOOLS/) | Place for shared scripts (empty for now) |

Parts 1 to 5 each have a `code` folder and a `doc` folder.

## Status

| Part | Status |
|---|---|
| ECU firmware, one part at a time | Camera (T-012), IMU (T-010), GNSS (T-014) and BLE to the phone app (T-016) each tested on the physical board ([test cases](5_TESTING/doc/README.md)); open items (for example the gyro limit, GNSS removal restart) are listed in each test file. The `report` firmware was tested on the ESP32-S3 emulator against the live backend |
| ECU firmware, all parts together (production build) | Builds with 0 warnings; **not yet run on the board** (TC-ECU-01 to 10) |
| Device logic (IMU detector, camera and IMU fusion, signing, replay window, report queue, NMEA parser) | 110 laptop tests on the firmware's own code; detector thresholds still to be tuned on real drives |
| Trust and consensus engine | Prototype, 54 tests, evaluated in simulation (results below) |
| Backend | Deployed on AWS Lambda with the team Supabase database: device registry, signed-report checks, replay defence, hazards ahead, sign-in and roles, reputation, operator dashboard |
| Camera detector, Pro unit (YOLO11n) | Best model v3c (fine-tuned on Egyptian day and night video): public test mAP50 0.846. Later runs v4 and v5 did not beat it |
| Camera detector, ESP32-S3 (ESPDet-Pico) | 8-bit model, 486 KB, speed-bump mAP50 0.72 in simulation; not yet run on the chip |
| Phone-to-phone and car-to-car messaging | Planned |

## ECU firmware: build and flash

One ESP-IDF v5.5.6 project in [`1_XAIO_SW/code`](1_XAIO_SW/code/). In an ESP-IDF terminal in that folder:

```bash
python build.py prod flash monitor -p <PORT>
```

`<PORT>` is `COM5` on Windows, `/dev/cu.usbmodem1101` on macOS, `/dev/ttyACM0` on Linux. Build types: `prod` (for
the car), `dev` (debug version) and the one-part test builds `smoke`, `camera`, `imu`, `gnss`, `ble`, `report`.
Step by step, with bring-up on a new board: [BUILD_AND_FLASH.md](1_XAIO_SW/doc/BUILD_AND_FLASH.md). Wiring and
reference: [1_XAIO_SW/doc/README.md](1_XAIO_SW/doc/README.md).

## Engine: simulated results

2,000 simulated runs per setting against two baselines (B1: warn on any report; B2: majority vote). Detector
assumptions: 85 % hit rate, 5 % false alarms. Weights and thresholds are not tuned yet.

| Attackers (share of cars) | B1 false alerts | B2 false alerts | **Engine false alerts** | Engine accuracy |
|---|---|---|---|---|
| 20 %, new identities | 46.9 % | 24.3 % | **0.5 %** | 96.0 % |
| 30 %, new identities | 49.2 % | 41.6 % | **1.0 %** | 94.0 % |
| 30 %, trusted "insider" identities | 49.2 % | 41.6 % | **15.2 %** (target 5 % or less) | 76.5 % |

The engine misses its target against insiders with a good history, which is the open research problem, and packet loss
raises missed hazards (8 % with no loss, 45.5 % at 30 % loss). These are simulated results, not real traffic. Details: [engine results](3_WEB_SW/code/engine/results/summary.md).

## Detector

YOLO11n trained on the public [SBP-YOLO dataset](https://github.com/chuanqi1997/SBP-YOLO) (near-duplicates removed),
then fine-tuned on Egyptian footage. On the public test set speed bumps are detected well and potholes are harder
(small, distant ones are missed), so camera detections are confirmed with the IMU and with other cars' reports. On
Egyptian roads many unpainted bumps are barely visible on camera, so the IMU is the main bump sensor there. Two
filters (is the box on the road, does it move like a fixed object) removed over 95 % of false bump flags on two
Egyptian test videos; the motion filter keeps about 4 in 5 of the real bumps the model finds (measured on training
videos). A larger Egyptian test set is still needed.

Models trained on private video (v3 and later, ESPDet-Pico) are kept out of this repository. Every run and its
numbers: [experiments.md](4_AI_SW/code/results/experiments.md). Training notebooks and scripts:
[4_AI_SW/code](4_AI_SW/code/README.md).

## Run the tests

Each line starts from the repo root:

```bash
(cd 3_WEB_SW/code/engine && python3 -m unittest)            # engine
(cd 3_WEB_SW/code/backend_sw && python3 -m unittest)        # backend (see its README for the database run)
(cd 5_TESTING/code/device_logic && pio test -e native)      # firmware logic on the laptop
```

Every push runs all checks on GitHub Actions, including a traceability check that links each test to its
requirement and fails if a test is unlisted or failing. Details: [5_TESTING/code/verification](5_TESTING/code/verification/).

## Next

1. Run the production firmware on the board (TC-ECU-01 to 10), then in a car
2. BLE follow-up tests (T-017): delay to the phone, bad messages, unknown phones
3. Record Egyptian drives (video, IMU, GNSS) to tune the IMU detector and test the camera
4. ESPDet-Pico on the chip; engine defence against insiders
5. Before real production: secure boot, flash encryption, updates over the air

## Credits and licences

- Dataset: C. Liang et al., "SBP-YOLO: a lightweight real-time model for detecting speed bumps and potholes
  toward intelligent vehicle suspension systems", *J. Real-Time Image Processing* 23, 52 (2026). Not included here.
- Detector: [Ultralytics YOLO](https://github.com/ultralytics/ultralytics) (AGPL-3.0).
- ESPDet-Pico and tools: Espressif [esp-detection](https://github.com/espressif/esp-detection),
  [ESP-PPQ](https://github.com/espressif/esp-ppq), [ESP-DL](https://github.com/espressif/esp-dl) and ESP-IDF.
- Road segmentation: SegFormer-B0 trained on Cityscapes (NVIDIA, via Hugging Face).
- Signatures: [Monocypher](https://monocypher.org) 4.0.2 (BSD-2 / CC0), in `1_XAIO_SW/code/components/icvsp_monocypher`.
