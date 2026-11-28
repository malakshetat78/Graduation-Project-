# T-012 Camera bring-up (ESP-IDF): test cases and results

- Ticket: T-012 Camera bring-up (development). Verification follows in T-013.
- Firmware: [components/icvsp_camera](../../../1_XAIO_SW/code/components/icvsp_camera) in the ESP-IDF project
  `1_XAIO_SW/code`, started by [main/camera_main.cpp](../../../1_XAIO_SW/code/main/camera_main.cpp). Selected with the
  build preset **T-012 camera test** (`sdkconfig.camera`).
- Tools: [extract_jpegs.py](../../code/camera/extract_jpegs.py) turns a saved log into photos and a summary;
  [run_host_tests.sh](../../code/camera/run_host_tests.sh) runs the host checks.
- Out of scope: hazard-detection accuracy. These tests only show that the camera produces valid images.

Every result states its setup: **physical board** or **host**. Only physical-board results count as camera
evidence.

## How to run it

1. VS Code with the ESP-IDF extension (ESP-IDF v5.5.x), open `1_XAIO_SW/code`.
2. `Cmd/Ctrl+Shift+P` > **ESP-IDF: Select Project Configuration** > **T-012 camera test**.
3. Write the light of the run: **ESP-IDF: SDK Configuration Editor** > search `Light` > for example `dim room` > Save.
4. Board still, pointing at the scene. Click **Build, Flash and Monitor** (flame icon).
5. Wait for `CAM RESULT pass=.. fail=..` (about 30 s). LED: slow blink = all passed, fast blink = a FAIL line.
6. Save everything the monitor printed to `camera_log_raw.txt`, then:
   `python3 5_TESTING/code/camera/extract_jpegs.py camera_log_raw.txt --out photos > extractor_summary.md`

Command line instead of VS Code:

```bash
. ~/esp/esp-idf/export.sh
cd 1_XAIO_SW/code
idf.py -B build_camera -D SDKCONFIG=build_camera/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.camera" set-target esp32s3 build
idf.py -B build_camera -p PORT flash monitor
```

## Test record

All runs: 2026-11-24, tester Shahesta, **physical board** XIAO ESP32-S3 Sense, camera OV3660 (PID 0x3660),
ESP-IDF v5.5.5, 20 photos per resolution, same board and USB cable. `firmware=16ad91a-dirty` in every log: the
code was not committed yet, so the version below says which code each run used.

| Run | Light / purpose | Firmware version | `CAM RESULT` | Folder |
| --- | --- | --- | --- | --- |
| 1 | bright room | A: first ESP-IDF port | pass=13 fail=0 | [run1_bright](run1_bright) |
| 2 | dim room | A | pass=13 fail=0 | [run2_dim](run2_dim) |
| 3 | lens covered | A | pass=13 fail=0 | [run3_lens_covered](run3_lens_covered) |
| 4 | bright room, repeat | A | pass=13 fail=0 | [run4_bright](run4_bright) |
| 5 | bright room, repeat (RESET only) | A | pass=13 fail=0 | [run5_bright](run5_bright) |
| 6 | camera removed | A | pass=3 fail=4 (expected) | [run6_no_camera](run6_no_camera) |
| 7 | camera put back, bright | B: stricter JPEG check, 2 SVGA buffers | pass=11 fail=2 | [run7_camera_back](run7_camera_back) |
| 8 | bright room | C: B + stuck pictures cut to the first | pass=13 fail=0 | [run8_bright_fixed](run8_bright_fixed) |
| 9 | bright room, repeat (RESET only) | C | pass=13 fail=0 | [run9_bright_fixed](run9_bright_fixed) |
| 10 | bright room, after an upload | D: C + PSRAM at 40 MHz | pass=13 fail=0 | [run10_psram40](run10_psram40) |
| 11 | dim room | E (final) | pass=13 fail=0, SVGA `cut=1` | [run11_dim_final](run11_dim_final) |
| 12 | lens covered | E (final) | pass=13 fail=0, VGA `cut=1`, SVGA `cut=4` | [run12_covered_final](run12_covered_final) |

Each folder has `camera_log_raw.txt` (unedited monitor output, colour codes removed), `extractor_summary.md` and
`photos/` (one QVGA and one VGA photo). Run 6 has no photos (no camera).

Firmware versions:

- **A**: the T-012 test moved to ESP-IDF. Its JPEG check looked at the start marker, headers and end marker only.
- **B**: the JPEG check also rejects a buffer that holds more than one picture (an end marker FF D9 inside the
  image data). SVGA uses 2 frame buffers instead of 1.
- **C**: `icvsp::get_photo()` cuts such a buffer to the first picture, which is complete on its own, and the test
  counts how often (`cut=`).
- **D**: same camera code as C. The board's memory chip (PSRAM) runs at 40 MHz instead of 80 MHz (problem 2).
  Camera speed is unchanged: QVGA wait 70.7 ms, SVGA 34.4 ms, start 347-352 ms.
- **E**: D + the JPEG check stops at a frame header too short to hold its fields, instead of reading past the end
  of the buffer (found in a code review, covered by a host test with AddressSanitizer).

## Test cases

| ID | Check | Expected | Actual | Pass/Fail | Evidence |
| --- | --- | --- | --- | --- | --- |
| TC-CAM-01 | Startup | `CAM startup PASS` on attempt 1, start time printed | 352.0-352.1 ms, attempt 1, in all 11 runs with a camera | Pass | every log, `CAM startup` |
| TC-CAM-02 | Sensor identity | `CAM sensor PASS`, OV3660, PID 0x3660 | `OV3660 PID=0x3660` in all 11 runs with a camera | Pass | every log, `CAM sensor` |
| TC-CAM-03 | Repeated capture QVGA | 20/20 valid 320x240 JPEGs | 20/20 in all 11 runs; no stuck pictures with the strict check (runs 7-12) | Pass | `CAM capture_QVGA` |
| TC-CAM-04 | Repeated capture VGA | 20/20 valid 640x480 JPEGs | Runs 8-12 (firmware C, D, E): 20/20 each; `cut=0` in bright light, `cut=1` covered (run 12). Run 7: 18/20, 2 buffers held 2 pictures | Pass (final) | `CAM capture_VGA` |
| TC-CAM-05 | Repeated capture SVGA | 20/20 valid 800x600 JPEGs | Runs 8-12: 20/20 each, no `FB-OVF`; `cut=0` in bright light, `cut=1` dim, `cut=4` covered (runs 11, 12). Run 7: 18/20, 2 stuck. Runs 1-5: see problem 1 | Pass (final) | `CAM capture_SVGA` |
| TC-CAM-06 | Image validity | Extractor verdict: one complete run, all checks passed, all photos OK; photos show the scene | Runs 1-5 and 7-12: all photos decoded fully with Pillow and OK (runs 4, 5, 7, 8, 9 and 12 were first saved without Pillow and re-checked on 2026-11-25 from the same logs; the photos came out byte for byte the same). Photos show the room (door, wall, clothes), dark in run 2, almost black in run 3 | Pass | `extractor_summary.md`, `photos/` |
| TC-CAM-07 | Capture timing | Start time, wait per photo and FPS recorded per resolution | Recorded below | Recorded | `CAMSTAT res=` |
| TC-CAM-08 | Memory use | Free RAM and PSRAM recorded; `CAM memory_back PASS` (<= 4096 bytes missing) | Internal 108-480 bytes, PSRAM 0 bytes missing after all tests, every run | Pass | `CAMSTAT mem`, `CAM memory_back` |
| TC-CAM-09 | Capture while stopped | `CAM fail_stopped PASS`, no crash | Pass in all 12 runs, also without a camera | Pass | `CAM fail_stopped` |
| TC-CAM-10 | Camera missing at power-on | 3 start attempts, error code, no crash, fast blink; after putting it back: camera works | Run 6: 3 attempts, `ESP_ERR_NOT_SUPPORTED (0x106)`, no crash, `pass=3 fail=4`. Run 7: started on attempt 1 | Pass | run6 and run7 logs |
| TC-CAM-11 | Forced start failure | `CAM fail_init PASS`: 2048x1536 buffer in internal RAM refused with `ESP_FAIL` | `ESP_FAIL (0xffffffff)`, largest internal block 262144 < 629145 needed, every run with a camera. The red `E (...) cam_hal ... frame buffer malloc failed` lines come from this case | Pass | `CAM fail_init` |
| TC-CAM-12 | Recovery | `CAM recover PASS`: camera starts again, valid 320x240 photo | Pass in every run with a camera (2992-4384 bytes) | Pass | `CAM recover` |
| TC-CAM-13 | Lighting | All capture checks pass in every light; lens covered gives valid but dark photos; sizes recorded | Final firmware: bright (runs 8-10), dim (run 11) and covered (run 12) all 13/13. Covered QVGA photo 2210 bytes, VGA 7578 bytes, almost black but valid. Firmware A runs 1-3 also passed, but its check missed stuck pictures (problem 1) | Pass | runs 8-12 |
| TC-CAM-14 | Repeatability | Same checks pass every run; timing within about 10 % in the same light | Runs 8 and 9 (firmware C, same scene, RESET only between them): every check passed; wait time and FPS equal, JPEG sizes within 2 % | Pass | runs 8, 9 |

## Measured results (physical board)

Wait per photo is the time `esp_camera_fb_get` took (min / avg / max). Sizes are JPEG bytes (min / avg / max).

| Run | Res | Buffers | Valid | Wait ms | FPS | Bytes |
| --- | --- | --- | --- | --- | --- | --- |
| 1 bright | QVGA | 1 | 20 | 51.6 / 70.6 / 72.1 | 13.9 | 4340 / 4371 / 4405 |
| 1 bright | VGA | 1 | 20 | 50.5 / 70.6 / 72.0 | 13.9 | 12464 / 12543 / 12619 |
| 1 bright | SVGA | 1 | 20 | 71.4 / 91.5 / 179.5 | 10.8 | 18141 / 21328 / 38730 |
| 2 dim | QVGA | 1 | 20 | 71.3 / 74.5 / 107.4 | 13.5 | 2998 / 3334 / 8151 |
| 2 dim | VGA | 1 | 20 | 49.8 / 70.5 / 72.1 | 13.9 | 12462 / 12872 / 13247 |
| 2 dim | SVGA | 1 | 20 | 71.4 / 77.0 / 107.6 | 12.9 | 20765 / 23423 / 44209 |
| 3 covered | QVGA | 1 | 20 | 58.3 / 71.0 / 72.0 | 13.9 | 2837 / 2904 / 3019 |
| 3 covered | VGA | 1 | 20 | 50.7 / 86.8 / 180.1 | 11.1 | 12374 / 16289 / 30900 |
| 3 covered | SVGA | 1 | 20 | 71.4 / 102.2 / 179.7 | 9.8 | 24004 / 32539 / 77293 |
| 4 bright | QVGA | 1 | 20 | 52.2 / 70.6 / 72.0 | 13.9 | 4225 / 4273 / 4321 |
| 4 bright | VGA | 1 | 20 | 50.2 / 70.5 / 72.0 | 13.9 | 12538 / 12651 / 12783 |
| 4 bright | SVGA | 1 | 20 | 71.5 / 78.8 / 143.4 | 12.9 | 18631 / 20811 / 59360 |
| 5 bright | QVGA | 1 | 20 | 51.8 / 70.6 / 72.0 | 13.9 | 4182 / 4243 / 4347 |
| 5 bright | VGA | 1 | 20 | 50.9 / 76.0 / 108.0 | 13.2 | 12468 / 12566 / 12699 |
| 5 bright | SVGA | 1 | 20 | 71.4 / 163.5 / 323.8 | 6.3 | 18314 / 34699 / 79644 |
| 7 camera back | QVGA | 1 | 20 | 51.6 / 72.1 / 107.3 | 13.5 | 4147 / 4216 / 4304 |
| 7 camera back | VGA | 1 | 18 | 51.1 / 76.8 / 108.2 | 12.8 | 11970 / 13655 / 26504 |
| 7 camera back | SVGA | 2 | 18 | 33.7 / 37.6 / 69.8 | 25.1 | 17553 / 19886 / 37300 |
| **8 bright** | QVGA | 1 | 20 | 51.4 / 70.3 / 72.4 | 13.9 | 4201 / 4253 / 4297 |
| **8 bright** | VGA | 1 | 20 | 48.8 / 69.5 / 73.1 | 13.9 | 12481 / 12585 / 12694 |
| **8 bright** | SVGA | 2 | 20 | 33.6 / 35.8 / 70.0 | 26.4 | 18637 / 18758 / 18993 |
| **9 bright** | QVGA | 1 | 20 | 51.7 / 70.3 / 72.4 | 13.9 | 4215 / 4248 / 4284 |
| **9 bright** | VGA | 1 | 20 | 48.8 / 69.5 / 73.2 | 13.9 | 12640 / 12747 / 12865 |
| **9 bright** | SVGA | 2 | 20 | 33.7 / 35.8 / 69.9 | 26.4 | 18744 / 18956 / 19107 |

Camera start (`esp_camera_init`): 346-354 ms at every resolution and run.

Memory (run 9; the same within a few hundred bytes in every run):

| Snapshot | Internal free | PSRAM free |
| --- | --- | --- |
| before_camera | 361835 | 8386192 |
| running_QVGA | 339495 | 8369624 |
| running_VGA | 339495 | 8323544 |
| running_SVGA (2 buffers) | 339495 | 8192924 |
| after_all_tests | 361507 | 8386192 |

## Problems found and fixed

1. **Pictures stuck together in one buffer.** Sometimes the camera driver returns a buffer that holds two or four
   pictures one after the other, so the photo is about 2x or 4x the normal size and took 2x or 4x as long. The
   first check (firmware A) only looked at the start and the end of the buffer, so it counted these as valid.
   The maximum sizes in runs 1-5 (VGA 30900, SVGA 38730-79644 bytes against about 12500 and 18700 normal) and
   the `cam_hal: FB-OVF` lines in run 5 (4 times) show it happened there too; the Arduino build had the same
   (SVGA photo of 87283 bytes, `FB-OVF` twice). Fixes:
   - The JPEG check rejects an end marker inside the image data (firmware B). Run 7 then showed it at VGA
     (2 of 20) and SVGA (2 of 20).
   - SVGA uses 2 frame buffers: no more `FB-OVF`, and the wait at SVGA went from 78-163 ms to 36 ms (26 FPS).
   - `icvsp::get_photo()` keeps only the first picture of such a buffer; it is complete and valid on its own.
     The rest of the firmware (and later the AI model) should take photos through this function.
   - Runs 8, 9 and 10 (bright light): no stuck buffers (`cut=0`, `stuck=0`). Runs 11 (dim) and 12 (lens
     covered) did show them: 1 cut at SVGA when dim, 1 at VGA and 4 at SVGA when covered. Every cut photo was
     valid and the runs passed 13/13, so the cutting works on the board. Dark photos are small, so two fit in one
     buffer more easily. The cutting is also tested on the host (below).
2. **Board stuck after an upload.** After one upload the board stopped while starting, just before it set up its
   memory chip (PSRAM), and a reset from the laptop did not help; only unplugging USB fixed it. The same happened
   once in the IMU tests, so 2 times in about 12 uploads. Change (firmware D): PSRAM speed 40 MHz instead of
   80 MHz (`CONFIG_SPIRAM_SPEED_40M=y` in `sdkconfig.defaults`). After that 6 of 6 uploads started on their own
   (run 10 here and 5 uploads in the IMU record, run 6), with the PSRAM memory test passing each time. This shows the change helped; it does not prove 80 MHz was
   the cause, since the freeze came only 2 times in about 12 uploads before. Separate
   from this, one smoke-test boot had a camera probe failure that worked after a reset; the start retries
   (3 attempts) cover that.

## Host checks (not camera evidence)

| Check | Setup | Command | Result | Date |
| --- | --- | --- | --- | --- |
| Camera firmware and smoke test build | host, ESP-IDF v5.5.5, target esp32s3 | `idf.py build` (both presets) | Pass, no warnings | 2026-11-24 |
| JPEG check and Base64 unit tests (30 cases: stuck pictures, stuffed FF 00, restart markers, cut to the first picture) | host, Apple clang, C++17 | `5_TESTING/code/camera/run_host_tests.sh` | Pass | 2026-11-24 |
| Log extractor unit tests (19 cases) | host, Python 3 | same script | Pass | 2026-11-24 |
| Real JPEG through the firmware's Base64 format and back, byte for byte | host | same script | Pass | 2026-11-24 |
| Strict JPEG check on every real photo from the board (22 photos, Arduino and ESP-IDF runs) | host | small test program | all valid, no stray markers | 2026-11-24 |
