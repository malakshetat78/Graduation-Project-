## Camera test log: camera_log_raw.txt

Result line: `CAM RESULT pass=13 fail=0`

| Check | Status | Details |
| --- | --- | --- |
| psram | PASS | 8192 KB |
| startup | PASS | init 352.1 ms (attempt 1) |
| sensor | PASS | OV3660 PID=0x3660 VER=0x00 MIDH=0x00 MIDL=0x00 (expected OV3660) |
| start_QVGA | PASS | init 348.0 ms (attempt 1) |
| capture_QVGA | PASS | 20/20 valid 320x240 JPEGs, wait avg 74.5 ms, 3334 bytes avg |
| start_VGA | PASS | init 350.5 ms (attempt 1) |
| capture_VGA | PASS | 20/20 valid 640x480 JPEGs, wait avg 70.5 ms, 12872 bytes avg |
| start_SVGA | PASS | init 346.7 ms (attempt 1) |
| capture_SVGA | PASS | 20/20 valid 800x600 JPEGs, wait avg 77.0 ms, 23423 bytes avg |
| fail_stopped | PASS | capture with camera stopped returned no frame, handled |
| fail_init | PASS | start refused: ESP_FAIL (0xffffffff) after 321.7 ms, largest internal block 262144 < needed 629145: frame buffer refused, as expected |
| recover | PASS | camera started again after the failed start, frame valid 320x240 (3169 bytes) |
| memory_back | PASS | after all tests: internal 480 bytes, PSRAM 0 bytes less than before the camera started (limit 4096) |

| Resolution | Valid / shots | Init ms | Wait ms min/avg/max | FPS | Bytes min/avg/max | Lighting |
| --- | --- | --- | --- | --- | --- | --- |
| QVGA 320x240 | 20/20 | 348.0 | 71.3/74.5/107.4 | 13.5 | 2998/3334/8151 | dim room |
| VGA 640x480 | 20/20 | 350.5 | 49.8/70.5/72.1 | 13.9 | 12462/12872/13247 | dim room |
| SVGA 800x600 | 20/20 | 346.7 | 71.4/77.0/107.6 | 12.9 | 20765/23423/44209 | dim room |

| Memory snapshot | Internal free | Internal largest block | PSRAM free | Lowest free ever |
| --- | --- | --- | --- | --- |
| before_camera | 361835 | 262144 | 8386192 | 8715996 |
| running_QVGA | 339495 | 262144 | 8369624 | 8672988 |
| running_VGA | 339495 | 262144 | 8323544 | 8613596 |
| running_SVGA | 339495 | 262144 | 8288984 | 8596448 |
| after_capture | 361355 | 262144 | 8386192 | 8596296 |
| after_all_tests | 361355 | 262144 | 8386192 | 8596296 |

Firmware `16ad91a-dirty`, ESP-IDF `v5.5.5`, built Nov 24 2026 22:55:38

| Image file | Size | Bytes | Decoded | Check |
| --- | --- | --- | --- | --- |
| 01_QVGA_shot01.jpg | 320x240 | 3032 | yes | OK |
| 02_VGA_shot01.jpg | 640x480 | 12723 | yes | OK |

Wrote 2 image(s) to ../../5_TESTING/doc/camera/run2_dim/photos/. Open them and check they show the scene.
Verdict: one complete run, all checks passed, all photos OK
