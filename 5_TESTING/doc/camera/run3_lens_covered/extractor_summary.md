## Camera test log: camera_log_raw.txt

Result line: `CAM RESULT pass=13 fail=0`

| Check | Status | Details |
| --- | --- | --- |
| psram | PASS | 8192 KB |
| startup | PASS | init 352.1 ms (attempt 1) |
| sensor | PASS | OV3660 PID=0x3660 VER=0x00 MIDH=0x00 MIDL=0x00 (expected OV3660) |
| start_QVGA | PASS | init 348.0 ms (attempt 1) |
| capture_QVGA | PASS | 20/20 valid 320x240 JPEGs, wait avg 71.0 ms, 2904 bytes avg |
| start_VGA | PASS | init 353.0 ms (attempt 1) |
| capture_VGA | PASS | 20/20 valid 640x480 JPEGs, wait avg 86.8 ms, 16289 bytes avg |
| start_SVGA | PASS | init 352.4 ms (attempt 1) |
| capture_SVGA | PASS | 20/20 valid 800x600 JPEGs, wait avg 102.2 ms, 32539 bytes avg |
| fail_stopped | PASS | capture with camera stopped returned no frame, handled |
| fail_init | PASS | start refused: ESP_FAIL (0xffffffff) after 317.5 ms, largest internal block 262144 < needed 629145: frame buffer refused, as expected |
| recover | PASS | camera started again after the failed start, frame valid 320x240 (2992 bytes) |
| memory_back | PASS | after all tests: internal 328 bytes, PSRAM 0 bytes less than before the camera started (limit 4096) |

| Resolution | Valid / shots | Init ms | Wait ms min/avg/max | FPS | Bytes min/avg/max | Lighting |
| --- | --- | --- | --- | --- | --- | --- |
| QVGA 320x240 | 20/20 | 348.0 | 58.3/71.0/72.0 | 13.9 | 2837/2904/3019 | lens covered |
| VGA 640x480 | 20/20 | 353.0 | 50.7/86.8/180.1 | 11.1 | 12374/16289/30900 | lens covered |
| SVGA 800x600 | 20/20 | 352.4 | 71.4/102.2/179.7 | 9.8 | 24004/32539/77293 | lens covered |

| Memory snapshot | Internal free | Internal largest block | PSRAM free | Lowest free ever |
| --- | --- | --- | --- | --- |
| before_camera | 361835 | 262144 | 8386192 | 8715996 |
| running_QVGA | 339495 | 262144 | 8369624 | 8673244 |
| running_VGA | 339495 | 262144 | 8323544 | 8614108 |
| running_SVGA | 339495 | 262144 | 8288984 | 8596448 |
| after_capture | 361507 | 262144 | 8386192 | 8596448 |
| after_all_tests | 361507 | 262144 | 8386192 | 8596448 |

Firmware `16ad91a-dirty`, ESP-IDF `v5.5.5`, built Nov 24 2026 23:05:30

| Image file | Size | Bytes | Decoded | Check |
| --- | --- | --- | --- | --- |
| 01_QVGA_shot01.jpg | 320x240 | 2838 | yes | OK |
| 02_VGA_shot01.jpg | 640x480 | 12626 | yes | OK |

Wrote 2 image(s) to ../../5_TESTING/doc/camera/run3_lens_covered/photos/. Open them and check they show the scene.
Verdict: one complete run, all checks passed, all photos OK
