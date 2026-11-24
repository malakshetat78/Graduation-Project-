## Camera test log: camera_log_raw.txt

Result line: `CAM RESULT pass=13 fail=0`

| Check | Status | Details |
| --- | --- | --- |
| psram | PASS | 8192 KB |
| startup | PASS | init 348.5 ms (attempt 1) |
| sensor | PASS | OV3660 PID=0x3660 VER=0x00 MIDH=0x00 MIDL=0x00 (expected OV3660) |
| start_QVGA | PASS | init 349.1 ms (attempt 1) |
| capture_QVGA | PASS | 20/20 valid 320x240 JPEGs, wait avg 71.7 ms, 2721 bytes avg |
| start_VGA | PASS | init 348.2 ms (attempt 1) |
| capture_VGA | PASS | 20/20 valid 640x480 JPEGs, wait avg 85.6 ms, 12221 bytes avg |
| start_SVGA | PASS | init 349.0 ms (attempt 1) |
| capture_SVGA | PASS | 20/20 valid 800x600 JPEGs, wait avg 82.7 ms, 17554 bytes avg |
| fail_stopped | PASS | capture with camera stopped returned no frame, handled |
| fail_init | PASS | start refused: ESP_FAIL (0xffffffff) after 324.0 ms, largest internal block 278516 < needed 629145: frame buffer refused, as expected |
| recover | PASS | camera started again after the failed start, frame valid 320x240 (3836 bytes) |
| memory_back | PASS | after all tests: internal 820 bytes, PSRAM 0 bytes less than before the camera started (limit 4096) |

| Resolution | Valid / shots | Init ms | Wait ms min/avg/max | FPS | Bytes min/avg/max | Lighting |
| --- | --- | --- | --- | --- | --- | --- |
| QVGA 320x240 | 20/20 | 349.1 | 68.0/71.7/72.0 | 13.9 | 2624/2721/2830 | bright room |
| VGA 640x480 | 20/20 | 348.2 | 71.8/85.6/144.1 | 11.9 | 8923/12221/31368 | bright room |
| SVGA 800x600 | 20/20 | 349.0 | 71.8/82.7/108.1 | 12.0 | 15455/17554/32994 | bright room |

| Memory snapshot | Internal free | Internal largest block | PSRAM free | Lowest free ever |
| --- | --- | --- | --- | --- |
| before_camera | 336188 | 278516 | 8386096 | 8717016 |
| running_QVGA | 312808 | 262132 | 8369360 | 8678580 |
| running_VGA | 312808 | 262132 | 8323280 | 8623028 |
| running_SVGA | 312608 | 262132 | 8288720 | 8601328 |
| after_capture | 335584 | 278516 | 8386096 | 8601328 |
| after_all_tests | 335368 | 278516 | 8386096 | 8601108 |

Firmware `arduino-3.3.12`, ESP-IDF `v5.5.5`, built Nov 24 2026 18:29:57

| Image file | Size | Bytes | Decoded | Check |
| --- | --- | --- | --- | --- |
| 01_QVGA_shot01.jpg | 320x240 | 2650 | yes | OK |
| 02_VGA_shot01.jpg | 640x480 | 9636 | yes | OK |

Wrote 2 image(s) to photos/. Open them and check they show the scene.
Verdict: one complete run, all checks passed, all photos OK
