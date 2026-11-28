## Camera test log: camera_log_raw.txt

Result line: `CAM RESULT pass=13 fail=0`

| Check | Status | Details |
| --- | --- | --- |
| psram | PASS | 8192 KB |
| startup | PASS | init 352.0 ms (attempt 1) |
| sensor | PASS | OV3660 PID=0x3660 VER=0x00 MIDH=0x00 MIDL=0x00 (expected OV3660) |
| start_QVGA | PASS | init 348.0 ms (attempt 1) |
| capture_QVGA | PASS | 20/20 valid 320x240 JPEGs, wait avg 70.6 ms, 3377 bytes avg |
| start_VGA | PASS | init 352.1 ms (attempt 1) |
| capture_VGA | PASS | 20/20 valid 640x480 JPEGs, wait avg 73.3 ms, 12021 bytes avg |
| start_SVGA | PASS | init 352.5 ms (attempt 1) |
| capture_SVGA | PASS | 20/20 valid 800x600 JPEGs, wait avg 35.7 ms, 19534 bytes avg, 1 buffer(s) held 2+ pictures and were cut to the first |
| fail_stopped | PASS | capture with camera stopped returned no frame, handled |
| fail_init | PASS | start refused: ESP_FAIL (0xffffffff) after 315.8 ms, largest internal block 270336 < needed 629145: frame buffer refused, as expected |
| recover | PASS | camera started again after the failed start, frame valid 320x240 (3197 bytes) |
| memory_back | PASS | after all tests: internal 480 bytes, PSRAM 0 bytes less than before the camera started (limit 4096) |

| Resolution | Valid / shots | Init ms | Wait ms min/avg/max | FPS | Bytes min/avg/max | Lighting |
| --- | --- | --- | --- | --- | --- | --- |
| QVGA 320x240 | 20/20 | 348.0 | 55.9/70.6/72.3 | 13.9 | 3314/3377/3457 | dim room |
| VGA 640x480 | 20/20 | 352.1 | 52.1/73.3/109.2 | 13.5 | 11667/12021/12266 | dim room |
| SVGA 800x600 | 20/20 | 352.5 | 31.5/35.7/71.8 | 26.4 | 18945/19534/19932 | dim room |

| Memory snapshot | Internal free | Internal largest block | PSRAM free | Lowest free ever |
| --- | --- | --- | --- | --- |
| before_camera | 364555 | 270336 | 8386192 | 8718716 |
| running_QVGA | 342063 | 262144 | 8369624 | 8675172 |
| running_VGA | 342063 | 262144 | 8323544 | 8617444 |
| running_SVGA | 342063 | 262144 | 8192924 | 8502956 |
| after_capture | 364075 | 270336 | 8386192 | 8502956 |
| after_all_tests | 364075 | 270336 | 8386192 | 8502956 |

Firmware `16ad91a-dirty`, ESP-IDF `v5.5.5`, built Nov 25 2026 00:37:22

| Image file | Size | Bytes | Decoded | Check |
| --- | --- | --- | --- | --- |
| 01_QVGA_shot01.jpg | 320x240 | 3351 | yes | OK |
| 02_VGA_shot01.jpg | 640x480 | 12012 | yes | OK |

Wrote 2 image(s) to photos/. Open them and check they show the scene.
Verdict: one complete run, all checks passed, all photos OK
