## Camera test log: camera_log_raw.txt

Result line: `CAM RESULT pass=13 fail=0`

| Check | Status | Details |
| --- | --- | --- |
| psram | PASS | 8192 KB |
| startup | PASS | init 352.0 ms (attempt 1) |
| sensor | PASS | OV3660 PID=0x3660 VER=0x00 MIDH=0x00 MIDL=0x00 (expected OV3660) |
| start_QVGA | PASS | init 348.0 ms (attempt 1) |
| capture_QVGA | PASS | 20/20 valid 320x240 JPEGs, wait avg 70.7 ms, 3167 bytes avg |
| start_VGA | PASS | init 352.0 ms (attempt 1) |
| capture_VGA | PASS | 20/20 valid 640x480 JPEGs, wait avg 74.1 ms, 9443 bytes avg |
| start_SVGA | PASS | init 346.9 ms (attempt 1) |
| capture_SVGA | PASS | 20/20 valid 800x600 JPEGs, wait avg 34.4 ms, 14173 bytes avg |
| fail_stopped | PASS | capture with camera stopped returned no frame, handled |
| fail_init | PASS | start refused: ESP_FAIL (0xffffffff) after 319.4 ms, largest internal block 270336 < needed 629145: frame buffer refused, as expected |
| recover | PASS | camera started again after the failed start, frame valid 320x240 (3181 bytes) |
| memory_back | PASS | after all tests: internal 328 bytes, PSRAM 0 bytes less than before the camera started (limit 4096) |

| Resolution | Valid / shots | Init ms | Wait ms min/avg/max | FPS | Bytes min/avg/max | Lighting |
| --- | --- | --- | --- | --- | --- | --- |
| QVGA 320x240 | 20/20 | 348.0 | 56.7/70.7/72.3 | 13.9 | 3138/3167/3212 | bright room |
| VGA 640x480 | 20/20 | 352.0 | 70.6/74.1/106.8 | 13.5 | 9320/9443/9537 | bright room |
| SVGA 800x600 | 20/20 | 346.9 | 34.2/34.4/37.2 | 27.8 | 14100/14173/14256 | bright room |

| Memory snapshot | Internal free | Internal largest block | PSRAM free | Lowest free ever |
| --- | --- | --- | --- | --- |
| before_camera | 364555 | 270336 | 8386192 | 8718716 |
| running_QVGA | 342215 | 262144 | 8369624 | 8675580 |
| running_VGA | 342215 | 262144 | 8323544 | 8620924 |
| running_SVGA | 342215 | 262144 | 8192924 | 8503108 |
| after_capture | 364227 | 270336 | 8386192 | 8503108 |
| after_all_tests | 364227 | 270336 | 8386192 | 8503108 |

Firmware `16ad91a-dirty`, ESP-IDF `v5.5.5`, built Nov 24 2026 23:50:14

| Image file | Size | Bytes | Decoded | Check |
| --- | --- | --- | --- | --- |
| 01_QVGA_shot01.jpg | 320x240 | 3158 | yes | OK |
| 02_VGA_shot01.jpg | 640x480 | 9507 | yes | OK |

Wrote 2 image(s) to photos/. Open them and check they show the scene.
Verdict: one complete run, all checks passed, all photos OK
