# T-012 Camera bring-up: test cases and results

- Ticket: T-012 Camera bring-up (development). Verification follows in T-013.
- Procedure: [Arduino sketch guide, camera section](../../../xaio_sw/arduino/ICVSP_ECU/START_HERE.md#camera-bring-up-test-t-012-no-wi-fi-needed)
- Firmware: [camera_test.cpp](../../../xaio_sw/arduino/ICVSP_ECU/src/app/camera_test.cpp), selected with `#define ICVSP_APP ICVSP_APP_CAMERA` in [config.h](../../../xaio_sw/arduino/ICVSP_ECU/config.h)
- Tools: [testing/hardware/camera/tools](tools/README.md)
- Out of scope: hazard-detection accuracy. These tests only show that the camera produces valid images.

Every result below states its setup: **physical board**, **emulator** or **host**. Only physical-board results
count as camera evidence.

## Test record (fill in for each board run)

| Field | Run 1 | Run 2 | Run 3 |
| --- | --- | --- | --- |
| Date | 2026-11-22 | | |
| Tester | Shahesta | | |
| Setup | physical board | physical board | physical board |
| Board | XIAO ESP32-S3 Sense | XIAO ESP32-S3 Sense | XIAO ESP32-S3 Sense |
| Camera sensor printed (name, PID) | OV3660, PID 0x3660 | | |
| Tested commit (`CAMSTAT build firmware=`) | Arduino sketch build, `firmware=arduino-3.3.13`, compiled Nov 22 2026 14:39:25 (uncommitted working copy) | | |
| ESP-IDF version (`idf=`) | v5.5.6-436-ga45de8ccca1 (inside Arduino core 3.3.13) | | |
| Lighting (`CONFIG_ICVSP_CAM_LIGHTING`) | bright room (ceiling lights on; log says "not recorded" because the Arduino build has no lighting setting) | dim room | lens covered |
| Scene | Room ceiling with spot lights, wall, plant, two bins; camera tilted, pointing up | | |
| `CAM RESULT` line | `CAM RESULT pass=13 fail=0` | | |
| Log file | [camera_log_raw.txt](run1_bright/camera_log_raw.txt) (unedited Serial Monitor output; a Smoke run comes first, the camera run starts at `CAM BEGIN`), [extractor_summary.md](run1_bright/extractor_summary.md) | | |
| Photos | [01_QVGA_shot01.jpg](run1_bright/photos/01_QVGA_shot01.jpg), [02_VGA_shot01.jpg](run1_bright/photos/02_VGA_shot01.jpg) | | |

Runs 1 to 3 differ only in lighting. Keep the board in the same place and pointed at the same scene.

## Test cases

| ID | Check | Steps | Expected | Actual | Pass/Fail | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| TC-CAM-01 | Startup | Flash the camera mode, open the monitor, press RESET | `CAM startup PASS` on attempt 1, start time printed. A start on attempt 2 or 3 prints `CAM startup FAIL ... started only after retries` | `CAM startup PASS init 358.7 ms (attempt 1)` | Pass | [run 1 log](run1_bright/camera_log_raw.txt) |
| TC-CAM-02 | Sensor identity | Same run | `CAM sensor PASS`, name OV3660, PID 0x3660 | `CAM sensor PASS OV3660 PID=0x3660` | Pass | [run 1 log](run1_bright/camera_log_raw.txt) |
| TC-CAM-03 | Repeated capture QVGA | Same run, board still, same scene | `CAM capture_QVGA PASS`, 20/20 valid 320x240 JPEGs | 20/20 valid 320x240, wait avg 71.6 ms, 5189 bytes avg | Pass | [run 1 log](run1_bright/camera_log_raw.txt), [QVGA photo](run1_bright/photos/01_QVGA_shot01.jpg) |
| TC-CAM-04 | Repeated capture VGA | Same run | `CAM capture_VGA PASS`, 20/20 valid 640x480 JPEGs | 20/20 valid 640x480, wait avg 81.7 ms, 15418 bytes avg | Pass | [run 1 log](run1_bright/camera_log_raw.txt), [VGA photo](run1_bright/photos/02_VGA_shot01.jpg) |
| TC-CAM-05 | Repeated capture SVGA | Same run | `CAM capture_SVGA PASS`, 20/20 valid 800x600 JPEGs | 20/20 valid 800x600, wait avg 93.6 ms, 27844 bytes avg. Driver printed `cam_hal: FB-OVF` twice (frame dropped by the driver, no invalid photo returned). No SVGA sample photo is printed by the firmware | Pass | [run 1 log](run1_bright/camera_log_raw.txt) |
| TC-CAM-06 | Image validity | Run `extract_jpegs.py` on the log (Pillow installed), open the `.jpg` files | Verdict line says one complete run, all checks passed, all photos OK; every image row says Decoded yes and OK; the photo shows the scene (not black, not garbled) | Verdict: one complete run, all checks passed, all photos OK. Both photos Decoded yes, OK. Photos show the room (ceiling lights, wall, plant, bins), not black or garbled | Pass | [extractor summary](run1_bright/extractor_summary.md), photos |
| TC-CAM-07 | Capture timing | Read the `CAMSTAT res=` lines (or the extractor table) | Start time, wait per photo and FPS recorded for each resolution. No pass mark; the values are the result | Recorded in the measured results below (run 1) | Recorded | [run 1 log](run1_bright/camera_log_raw.txt) |
| TC-CAM-08 | Memory use | Read the `CAMSTAT mem` lines | Free internal RAM and PSRAM recorded before, while running and after. `CAM memory_back PASS` (no more than 4096 bytes missing) | Recorded below. `CAM memory_back PASS` internal 620 bytes, PSRAM 0 bytes less than before | Pass | [run 1 log](run1_bright/camera_log_raw.txt) |
| TC-CAM-09 | Capture while stopped | Same run, step 5 | `CAM fail_stopped PASS`: no photo returned, no crash or reboot | `CAM fail_stopped PASS` capture with camera stopped returned no frame, handled; no reboot | Pass | [run 1 log](run1_bright/camera_log_raw.txt) |
| TC-CAM-10 | Camera missing at power-on | **Unplug USB.** Remove the camera ribbon (or the Sense expansion board). Plug USB in, open the monitor, press RESET. Then unplug USB, reconnect the camera, plug in, RESET | Without camera: 3 start attempts, `CAM startup FAIL` with the error code (0x105 or 0x106), no crash, `CAM RESULT ... fail>0`, fast LED blink. After reconnecting: `CAM RESULT pass=13 fail=0` | | | |
| TC-CAM-11 | Forced start failure | Same run as TC-CAM-01, step 5 | `CAM fail_init PASS`: start with a 2048x1536 buffer in internal RAM refused with `ESP_FAIL`, and the largest free internal block printed is smaller than the 629145 bytes needed | `CAM fail_init PASS` ESP_FAIL (0xffffffff) after 331.5 ms, largest internal block 278516 < 629145 needed | Pass | [run 1 log](run1_bright/camera_log_raw.txt) |
| TC-CAM-12 | Recovery | Same run, step 5 | `CAM recover PASS`: camera starts again and, after the warm-up frames, gives a complete 320x240 photo of at least 512 bytes | `CAM recover PASS` camera started again, frame valid 320x240 (5462 bytes) | Pass | [run 1 log](run1_bright/camera_log_raw.txt) |
| TC-CAM-13 | Lighting | Runs 1, 2, 3 of the test record | All capture checks pass in every light. Lens covered gives valid but dark photos. Record JPEG sizes per light | | | |
| TC-CAM-14 | Repeatability | Compare runs 1 to 3 (and RESET twice more in bright light) | Same checks pass on every run. Timing within about 10 % between runs in the same light | | | |

## Measured results (physical board)

Run 1 (bright room, 2026-11-22) from the [extractor summary](run1_bright/extractor_summary.md).
Dim and lens-covered rows stay empty until runs 2 and 3.

| Measurement | QVGA 320x240 | VGA 640x480 | SVGA 800x600 |
| --- | --- | --- | --- |
| Camera start, ms | 358.5 | 357.9 | 358.5 |
| Wait per photo, ms (min / avg / max) | 63.6 / 71.6 / 72.1 | 50.8 / 81.7 / 108.3 | 70.0 / 93.6 / 218.5 |
| Frames per second | 13.9 | 11.9 | 10.8 |
| JPEG size bright, bytes (min / avg / max) | 5137 / 5189 / 5237 | 10909 / 15418 / 30183 | 18003 / 27844 / 87283 |
| JPEG size dim, bytes (avg) | | | |
| JPEG size lens covered, bytes (avg) | | | |
| Valid photos / taken | 20 / 20 | 20 / 20 | 20 / 20 |
| Free internal RAM while running, bytes | 310884 | 310884 | 310884 |
| Free PSRAM while running, bytes | 8370164 | 8324084 | 8289524 |

| Memory snapshot | Internal free | PSRAM free |
| --- | --- | --- |
| before_camera | 335068 | 8386096 |
| after_capture | 334664 | 8386096 |
| after_all_tests | 334448 | 8386096 |

Observations from run 1:

- The photos are tilted and the JPEG sizes vary a lot at VGA and SVGA, which suggests the board moved during the run
  (to be confirmed by the tester). Runs 2 and 3 should keep the board still, as the procedure says.
- During SVGA capture the driver printed `cam_hal: FB-OVF` twice: a frame did not fit the buffer and was dropped by
  the driver. All 20 returned photos were valid. The 218.5 ms maximum wait at SVGA is probably one of those drops.
- The firmware prints sample photos for QVGA and VGA only, so there is no SVGA photo to look at.

## Host checks (not camera evidence)

These run without the board. They check the code paths around the camera, not the camera.

| Check | Setup | Command | Result | Date |
| --- | --- | --- | --- | --- |
| Sketch compiles for every program (smoke, report, camera, IMU, BLE) | host, Arduino CLI, esp32 core for XIAO_ESP32S3, PSRAM OPI, NimBLE-Arduino 2.5.2 | `arduino-cli compile` per `ICVSP_APP` value | Pass: all five compile (camera 371359 bytes, BLE 561950, report 1108482) | 2026-11-23 |
| JPEG check and Base64 unit tests (22 cases, including malformed frame and scan headers) | host, Apple clang, C++17 | `testing/hardware/camera/tools/run_host_tests.sh` | Pass | 2026-11-23 |
| Log extractor unit tests (19 cases: image structure, Pillow decode, run verdict) | host, Python 3.13, Pillow 12.2 | same script | Pass | 2026-11-23 |
| Real JPEG through the firmware's Base64 format and back, byte for byte | host | same script | Pass | 2026-11-23 |
