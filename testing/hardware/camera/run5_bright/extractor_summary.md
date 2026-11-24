## Camera test log: camera_log_raw.txt

Result line: `CAM RESULT pass=13 fail=0`

| Check | Status | Details |
| --- | --- | --- |
| psram | PASS | 8192 KB |
| startup | PASS | init 349.0 ms (attempt 1) |
| sensor | PASS | OV3660 PID=0x3660 VER=0x00 MIDH=0x00 MIDL=0x00 (expected OV3660) |
| start_QVGA | PASS | init 349.1 ms (attempt 1) |
| capture_QVGA | PASS | 20/20 valid 320x240 JPEGs, wait avg 71.6 ms, 4738 bytes avg |
| start_VGA | PASS | init 348.2 ms (attempt 1) |
| capture_VGA | PASS | 20/20 valid 640x480 JPEGs, wait avg 71.0 ms, 13026 bytes avg |
| start_SVGA | PASS | init 349.1 ms (attempt 1) |
| capture_SVGA | PASS | 20/20 valid 800x600 JPEGs, wait avg 71.9 ms, 18315 bytes avg |
| fail_stopped | PASS | capture with camera stopped returned no frame, handled |
| fail_init | PASS | start refused: ESP_FAIL (0xffffffff) after 324.3 ms, largest internal block 278516 < needed 629145: frame buffer refused, as expected |
| recover | PASS | camera started again after the failed start, frame valid 320x240 (4772 bytes) |
| memory_back | PASS | after all tests: internal 620 bytes, PSRAM 0 bytes less than before the camera started (limit 4096) |

| Resolution | Valid / shots | Init ms | Wait ms min/avg/max | FPS | Bytes min/avg/max | Lighting |
| --- | --- | --- | --- | --- | --- | --- |
| QVGA 320x240 | 20/20 | 349.1 | 64.9/71.6/72.0 | 13.9 | 4714/4738/4760 | bright room |
| VGA 640x480 | 20/20 | 348.2 | 52.7/71.0/72.0 | 13.9 | 12989/13026/13063 | bright room |
| SVGA 800x600 | 20/20 | 349.1 | 71.8/71.9/72.0 | 13.9 | 18258/18315/18379 | bright room |

| Memory snapshot | Internal free | Internal largest block | PSRAM free | Lowest free ever |
| --- | --- | --- | --- | --- |
| before_camera | 336188 | 278516 | 8386096 | 8717016 |
| running_QVGA | 312808 | 262132 | 8369360 | 8675764 |
| running_VGA | 312808 | 262132 | 8323280 | 8618164 |
| running_SVGA | 312808 | 262132 | 8288720 | 8601528 |
| after_capture | 335784 | 278516 | 8386096 | 8601528 |
| after_all_tests | 335568 | 278516 | 8386096 | 8601312 |

Firmware `arduino-3.3.12`, ESP-IDF `v5.5.5`, built Nov 24 2026 18:29:57

| Image file | Size | Bytes | Decoded | Check |
| --- | --- | --- | --- | --- |
| 01_QVGA_shot01.jpg | 320x240 | 4757 | yes | OK |
| 02_VGA_shot01.jpg | 640x480 | 13050 | yes | OK |

Wrote 2 image(s) to photos/. Open them and check they show the scene.
Verdict: one complete run, all checks passed, all photos OK
