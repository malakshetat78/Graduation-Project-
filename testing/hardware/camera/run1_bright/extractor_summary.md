## Camera test log: camera_log_raw.txt

Result line: `CAM RESULT pass=13 fail=0`

| Check | Status | Details |
| --- | --- | --- |
| psram | PASS | 8192 KB |
| startup | PASS | init 358.7 ms (attempt 1) |
| sensor | PASS | OV3660 PID=0x3660 VER=0x00 MIDH=0x00 MIDL=0x00 (expected OV3660) |
| start_QVGA | PASS | init 358.5 ms (attempt 1) |
| capture_QVGA | PASS | 20/20 valid 320x240 JPEGs, wait avg 71.6 ms, 5189 bytes avg |
| start_VGA | PASS | init 357.9 ms (attempt 1) |
| capture_VGA | PASS | 20/20 valid 640x480 JPEGs, wait avg 81.7 ms, 15418 bytes avg |
| start_SVGA | PASS | init 358.5 ms (attempt 1) |
| capture_SVGA | PASS | 20/20 valid 800x600 JPEGs, wait avg 93.6 ms, 27844 bytes avg |
| fail_stopped | PASS | capture with camera stopped returned no frame, handled |
| fail_init | PASS | start refused: ESP_FAIL (0xffffffff) after 331.5 ms, largest internal block 278516 < needed 629145: frame buffer refused, as expected |
| recover | PASS | camera started again after the failed start, frame valid 320x240 (5462 bytes) |
| memory_back | PASS | after all tests: internal 620 bytes, PSRAM 0 bytes less than before the camera started (limit 4096) |

| Resolution | Valid / shots | Init ms | Wait ms min/avg/max | FPS | Bytes min/avg/max | Lighting |
| --- | --- | --- | --- | --- | --- | --- |
| QVGA 320x240 | 20/20 | 358.5 | 63.6/71.6/72.1 | 13.9 | 5137/5189/5237 | not recorded |
| VGA 640x480 | 20/20 | 357.9 | 50.8/81.7/108.3 | 11.9 | 10909/15418/30183 | not recorded |
| SVGA 800x600 | 20/20 | 358.5 | 70.0/93.6/218.5 | 10.8 | 18003/27844/87283 | not recorded |

| Memory snapshot | Internal free | Internal largest block | PSRAM free | Lowest free ever |
| --- | --- | --- | --- | --- |
| before_camera | 335068 | 278516 | 8386096 | 8715908 |
| running_QVGA | 310884 | 262132 | 8370164 | 8674004 |
| running_VGA | 310884 | 262132 | 8324084 | 8617044 |
| running_SVGA | 310884 | 262132 | 8289524 | 8600408 |
| after_capture | 334664 | 278516 | 8386096 | 8600408 |
| after_all_tests | 334448 | 278516 | 8386096 | 8600192 |

Firmware `arduino-3.3.13`, ESP-IDF `v5.5.6-436-ga45de8ccca1`, built Nov 22 2026 14:39:25

| Image file | Size | Bytes | Decoded | Check |
| --- | --- | --- | --- | --- |
| 01_QVGA_shot01.jpg | 320x240 | 5237 | yes | OK |
| 02_VGA_shot01.jpg | 640x480 | 13110 | yes | OK |

Wrote 2 image(s) to 5_TESTING/results/T-012/run1_bright/photos/. Open them and check they show the scene.
Verdict: one complete run, all checks passed, all photos OK
