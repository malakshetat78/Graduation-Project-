# T-012 Camera bring-up: test cases and results

- Ticket: T-012 Camera bring-up (development). Verification follows in T-013.
- Procedure: [Arduino sketch guide, camera section](../../../xaio_sw/arduino/ICVSP_ECU/START_HERE.md#camera-bring-up-test-t-012-no-wi-fi-needed)
- Firmware: [camera_test.cpp](../../../xaio_sw/arduino/ICVSP_ECU/src/app/camera_test.cpp), selected with `#define ICVSP_APP ICVSP_APP_CAMERA` in [config.h](../../../xaio_sw/arduino/ICVSP_ECU/config.h)
- Tools: [testing/hardware/camera/tools](tools/README.md)
- Out of scope: hazard-detection accuracy. These tests only show that the camera produces valid images.

Every result below states its setup: **physical board**, **emulator** or **host**. Only physical-board results
count as camera evidence.

## Test record

All runs: Shahesta, physical board, XIAO ESP32-S3 Sense, camera sensor OV3660 (PID 0x3660), same place and scene
(room ceiling, wall, lights). Each log is Serial Monitor output from the `CAM BEGIN` line on (the lines of the
previous run that were still in the monitor are cut off), with the extractor summary and the two sample photos.

| Run | Date | Purpose | Lighting (`CONFIG_ICVSP_CAM_LIGHTING`) | Build (`CAMSTAT build`) | `CAM RESULT` | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 2026-11-22 | first run (TC-CAM-01 to 12) | bright room, ceiling lights on (log says "not recorded": no lighting setting in that build) | `arduino-3.3.13`, IDF v5.5.6-436-ga45de8ccca1, compiled Nov 22 2026 14:39:25 | `pass=13 fail=0` | [log](run1_bright/camera_log_raw.txt) (a Smoke run comes first), [summary](run1_bright/extractor_summary.md), [QVGA](run1_bright/photos/01_QVGA_shot01.jpg), [VGA](run1_bright/photos/02_VGA_shot01.jpg) |
| 2 | 2026-11-24 | lighting (TC-CAM-13) | dim room (lens partly covered, the room could not be darkened) | `arduino-3.3.12`, IDF v5.5.5, compiled Nov 24 2026 18:19:17 | `pass=13 fail=0` | [log](run2_dim/camera_log_raw.txt), [summary](run2_dim/extractor_summary.md), [QVGA](run2_dim/photos/01_QVGA_shot01.jpg), [VGA](run2_dim/photos/02_VGA_shot01.jpg) |
| 3 | 2026-11-24 | lighting (TC-CAM-13) | lens covered | same core, compiled Nov 24 2026 18:26:57 | `pass=13 fail=0` | [log](run3_lens_covered/camera_log_raw.txt), [summary](run3_lens_covered/extractor_summary.md), [QVGA](run3_lens_covered/photos/01_QVGA_shot01.jpg), [VGA](run3_lens_covered/photos/02_VGA_shot01.jpg) |
| 4 | 2026-11-24 | repeatability (TC-CAM-14) | bright room | same core, compiled Nov 24 2026 18:29:57 | `pass=13 fail=0` | [log](run4_bright/camera_log_raw.txt), [summary](run4_bright/extractor_summary.md), [QVGA](run4_bright/photos/01_QVGA_shot01.jpg), [VGA](run4_bright/photos/02_VGA_shot01.jpg) |
| 5 | 2026-11-24 | repeatability (TC-CAM-14), RESET only, same build as run 4 | bright room | same as run 4 | `pass=13 fail=0` | [log](run5_bright/camera_log_raw.txt), [summary](run5_bright/extractor_summary.md), [QVGA](run5_bright/photos/01_QVGA_shot01.jpg), [VGA](run5_bright/photos/02_VGA_shot01.jpg) |
| 6 | 2026-11-24 | camera missing (TC-CAM-10): USB unplugged, camera removed, USB back in, RESET | (no camera) | same as run 4 | `pass=3 fail=4` (expected) | [log](run6_no_camera/camera_log_raw.txt), [summary](run6_no_camera/extractor_summary.md) |
| 7 | 2026-11-24 | camera reconnected (TC-CAM-10): USB unplugged, camera back on, USB back in, RESET | label "bright room", but the photos show a dark close-up: the board was probably moved when the camera was put back | same as run 4 | `pass=13 fail=0` | [log](run7_camera_back/camera_log_raw.txt), [summary](run7_camera_back/extractor_summary.md), [QVGA](run7_camera_back/photos/01_QVGA_shot01.jpg), [VGA](run7_camera_back/photos/02_VGA_shot01.jpg) |

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
| TC-CAM-10 | Camera missing at power-on | **Unplug USB.** Remove the camera ribbon (or the Sense expansion board). Plug USB in, open the monitor, press RESET. Then unplug USB, reconnect the camera, plug in, RESET | Without camera: 3 start attempts, `CAM startup FAIL` with the error code (0x105 or 0x106), no crash, `CAM RESULT ... fail>0`, fast LED blink. After reconnecting: `CAM RESULT pass=13 fail=0` | Run 6 (no camera): 3 attempts, each `ESP_ERR_NOT_SUPPORTED (0x106)`, `CAM startup FAIL camera did not start after 3 attempts`, capture/fail_init/recover skipped, `CAM RESULT pass=3 fail=4`, `LED: fast blink`, no `Guru Meditation` or reboot. Run 7 (camera back): `CAM startup PASS init 348.5 ms (attempt 1)`, `CAM RESULT pass=13 fail=0` | Pass | [run 6 log](run6_no_camera/camera_log_raw.txt) lines 11-18, 34; [run 7 log](run7_camera_back/camera_log_raw.txt) lines 9, 322 |
| TC-CAM-11 | Forced start failure | Same run as TC-CAM-01, step 5 | `CAM fail_init PASS`: start with a 2048x1536 buffer in internal RAM refused with `ESP_FAIL`, and the largest free internal block printed is smaller than the 629145 bytes needed | `CAM fail_init PASS` ESP_FAIL (0xffffffff) after 331.5 ms, largest internal block 278516 < 629145 needed | Pass | [run 1 log](run1_bright/camera_log_raw.txt) |
| TC-CAM-12 | Recovery | Same run, step 5 | `CAM recover PASS`: camera starts again and, after the warm-up frames, gives a complete 320x240 photo of at least 512 bytes | `CAM recover PASS` camera started again, frame valid 320x240 (5462 bytes) | Pass | [run 1 log](run1_bright/camera_log_raw.txt) |
| TC-CAM-13 | Lighting | Runs 1 (or 4), 2, 3 of the test record | All capture checks pass in every light. Lens covered gives valid but dark photos. Record JPEG sizes per light | All 13 checks pass in bright (runs 1, 4, 5), dim (run 2) and lens covered (run 3). All photos decode. Dim photos are dark and blurry, lens-covered photos almost black. JPEG sizes in the measured results below: dim is 25-40 % smaller than bright (runs 4/5). Lens covered is the smallest at QVGA but larger than dim at VGA and SVGA, with large jumps at SVGA (up to 36416 bytes): in the dark the sensor turns its gain up and the noise makes the JPEG bigger | Pass | [run 2 summary](run2_dim/extractor_summary.md), [run 3 summary](run3_lens_covered/extractor_summary.md), [run 4 summary](run4_bright/extractor_summary.md), photos |
| TC-CAM-14 | Repeatability | Compare runs 1 to 3 (and RESET twice more in bright light) | Same checks pass on every run. Timing within about 10 % between runs in the same light | All 13 checks pass on runs 1 to 5 and 7. Runs 4 and 5 (same build, same light, RESET only): camera start 348.2-349.2 ms in both; wait avg QVGA 71.6/71.6 ms, VGA 71.0/71.0 ms, SVGA 73.7/71.9 ms (2.4 %); JPEG size avg within 1 % at every resolution. Run 1 is slower at VGA/SVGA (81.7 and 93.6 ms) probably because the board moved during that run (see observations), and it used core 3.3.13 | Pass | [run 4 summary](run4_bright/extractor_summary.md), [run 5 summary](run5_bright/extractor_summary.md) |

## Measured results (physical board)

Run 1 (bright room, 2026-11-22) from the [extractor summary](run1_bright/extractor_summary.md). The dim, lens-covered and
run 4/5 rows come from the summaries of runs 2 to 5.

| Measurement | QVGA 320x240 | VGA 640x480 | SVGA 800x600 |
| --- | --- | --- | --- |
| Camera start run 1, ms | 358.5 | 357.9 | 358.5 |
| Wait per photo run 1, ms (min / avg / max) | 63.6 / 71.6 / 72.1 | 50.8 / 81.7 / 108.3 | 70.0 / 93.6 / 218.5 |
| Frames per second run 1 | 13.9 | 11.9 | 10.8 |
| JPEG size bright run 1, bytes (min / avg / max) | 5137 / 5189 / 5237 | 10909 / 15418 / 30183 | 18003 / 27844 / 87283 |
| JPEG size bright run 4, bytes (min / avg / max) | 4668 / 4696 / 4718 | 12979 / 13066 / 13188 | 18208 / 18296 / 18382 |
| JPEG size bright run 5, bytes (min / avg / max) | 4714 / 4738 / 4760 | 12989 / 13026 / 13063 | 18258 / 18315 / 18379 |
| JPEG size dim (run 2), bytes (min / avg / max) | 2849 / 2914 / 2991 | 8175 / 8652 / 9102 | 12941 / 13719 / 14287 |
| JPEG size lens covered (run 3), bytes (min / avg / max) | 2229 / 2361 / 2612 | 8645 / 9847 / 10961 | 13566 / 20153 / 36416 |
| Wait per photo runs 4 / 5, ms (avg) | 71.6 / 71.6 | 71.0 / 71.0 | 73.7 / 71.9 |
| Wait per photo dim / lens covered, ms (avg) | 71.7 / 71.7 | 71.3 / 72.9 | 80.9 / 86.3 |
| Valid photos / taken (every run with a camera) | 20 / 20 | 20 / 20 | 20 / 20 |
| Free internal RAM while running run 1, bytes | 310884 | 310884 | 310884 |
| Free PSRAM while running run 1, bytes | 8370164 | 8324084 | 8289524 |

| Memory snapshot (run 1) | Internal free | PSRAM free |
| --- | --- | --- |
| before_camera | 335068 | 8386096 |
| after_capture | 334664 | 8386096 |
| after_all_tests | 334448 | 8386096 |

Observations:

- Run 1: the photos are tilted and the JPEG sizes vary a lot at VGA and SVGA, which suggests the board moved during
  the run.
- Run 1: during SVGA capture the driver printed `cam_hal: FB-OVF` twice: a frame did not fit the buffer and was dropped by
  the driver. All 20 returned photos were valid. The 218.5 ms maximum wait at SVGA is probably one of those drops.
- The firmware prints sample photos for QVGA and VGA only, so there is no SVGA photo to look at.
- Runs 4 and 5 kept the board still: JPEG sizes changed by less than 1 % between shots, and SVGA ran at 13.5-13.9
  FPS with no `cam_hal: FB-OVF` lines. The board moving in run 1 probably explains its slower VGA/SVGA numbers.
- Waits of about 108 ms and 144 ms (one or two frames late) appear at VGA/SVGA in the dim, lens-covered and run 7
  logs, together with bigger frames. No frame was invalid.
- Memory is the same on every run: before the camera 336188 bytes internal free, `memory_back` 620 bytes less
  (820 in run 7), PSRAM 0 bytes less.
- Run 7's photos are a dark close-up although the label says bright room: the board was probably moved when the
  camera was put back. Run 7 only counts for TC-CAM-10 (the camera works again), not for the lighting or timing comparisons.

## Host checks (not camera evidence)

These run without the board. They check the code paths around the camera, not the camera.

| Check | Setup | Command | Result | Date |
| --- | --- | --- | --- | --- |
| Sketch compiles for every program (smoke, report, camera, IMU, BLE) | host, Arduino CLI, esp32 core for XIAO_ESP32S3, PSRAM OPI, NimBLE-Arduino 2.5.2 | `arduino-cli compile` per `ICVSP_APP` value | Pass: all five compile (camera 371359 bytes, BLE 561950, report 1108482) | 2026-11-23 |
| JPEG check and Base64 unit tests (22 cases, including malformed frame and scan headers) | host, Apple clang, C++17 | `testing/hardware/camera/tools/run_host_tests.sh` | Pass | 2026-11-23 |
| Log extractor unit tests (19 cases: image structure, Pillow decode, run verdict) | host, Python 3.13, Pillow 12.2 | same script | Pass | 2026-11-23 |
| Real JPEG through the firmware's Base64 format and back, byte for byte | host | same script | Pass | 2026-11-23 |
