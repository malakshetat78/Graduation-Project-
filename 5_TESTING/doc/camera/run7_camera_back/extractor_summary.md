## Camera test log: camera_log_raw.txt

Result line: `CAM RESULT pass=11 fail=2`

| Check | Status | Details |
| --- | --- | --- |
| psram | PASS | 8192 KB |
| startup | PASS | init 352.0 ms (attempt 1) |
| sensor | PASS | OV3660 PID=0x3660 VER=0x00 MIDH=0x00 MIDL=0x00 (expected OV3660) |
| start_QVGA | PASS | init 348.1 ms (attempt 1) |
| capture_QVGA | PASS | 20/20 valid 320x240 JPEGs, wait avg 72.1 ms, 4216 bytes avg |
| start_VGA | PASS | init 346.2 ms (attempt 1) |
| capture_VGA | FAIL | 18/20 valid 640x480 JPEGs, wait avg 76.8 ms, 13655 bytes avg, some buffers held more than one picture |
| start_SVGA | PASS | init 349.8 ms (attempt 1) |
| capture_SVGA | FAIL | 18/20 valid 800x600 JPEGs, wait avg 37.6 ms, 19886 bytes avg, some buffers held more than one picture |
| fail_stopped | PASS | capture with camera stopped returned no frame, handled |
| fail_init | PASS | start refused: ESP_FAIL (0xffffffff) after 318.1 ms, largest internal block 262144 < needed 629145: frame buffer refused, as expected |
| recover | PASS | camera started again after the failed start, frame valid 320x240 (4177 bytes) |
| memory_back | PASS | after all tests: internal 328 bytes, PSRAM 0 bytes less than before the camera started (limit 4096) |

| Resolution | Valid / shots | Init ms | Wait ms min/avg/max | FPS | Bytes min/avg/max | Lighting |
| --- | --- | --- | --- | --- | --- | --- |
| QVGA 320x240 | 20/20 | 348.1 | 51.6/72.1/107.3 | 13.5 | 4147/4216/4304 | bright room |
| VGA 640x480 | 18/20 | 346.2 | 51.1/76.8/108.2 | 12.8 | 11970/13655/26504 | bright room |
| SVGA 800x600 | 18/20 | 349.8 | 33.7/37.6/69.8 | 25.1 | 17553/19886/37300 | bright room |

| Memory snapshot | Internal free | Internal largest block | PSRAM free | Lowest free ever |
| --- | --- | --- | --- | --- |
| before_camera | 361835 | 262144 | 8386192 | 8715996 |
| running_QVGA | 339495 | 262144 | 8369624 | 8671324 |
| running_VGA | 339495 | 262144 | 8323544 | 8614620 |
| running_SVGA | 339495 | 262144 | 8192924 | 8500388 |
| after_capture | 361507 | 262144 | 8386192 | 8500388 |
| after_all_tests | 361507 | 262144 | 8386192 | 8500388 |

Firmware `16ad91a-dirty`, ESP-IDF `v5.5.5`, built Nov 24 2026 23:07:36

| Image file | Size | Bytes | Decoded | Check |
| --- | --- | --- | --- | --- |
| 01_QVGA_shot01.jpg | 320x240 | 4261 | yes (re-checked with Pillow 12.2.0 on 2026-11-25) | OK |
| 02_VGA_shot01.jpg | 640x480 | 12204 | yes (re-checked with Pillow 12.2.0 on 2026-11-25) | OK |

RUN PROBLEM: the firmware reported 2 failed check(s)

RUN PROBLEM: FAIL lines: capture_VGA, capture_SVGA

Wrote 2 image(s) to ../../5_TESTING/doc/camera/run7_camera_back/photos/. Open them and check they show the scene.
Verdict: NOT a complete passing run (see the problems above)
