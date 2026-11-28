## Camera test log: camera_log_raw.txt

Result line: `CAM RESULT pass=13 fail=0`

| Check | Status | Details |
| --- | --- | --- |
| psram | PASS | 8192 KB |
| startup | PASS | init 352.0 ms (attempt 1) |
| sensor | PASS | OV3660 PID=0x3660 VER=0x00 MIDH=0x00 MIDL=0x00 (expected OV3660) |
| start_QVGA | PASS | init 348.0 ms (attempt 1) |
| capture_QVGA | PASS | 20/20 valid 320x240 JPEGs, wait avg 70.9 ms, 2215 bytes avg |
| start_VGA | PASS | init 352.3 ms (attempt 1) |
| capture_VGA | PASS | 20/20 valid 640x480 JPEGs, wait avg 72.9 ms, 7525 bytes avg, 1 buffer(s) held 2+ pictures and were cut to the first |
| start_SVGA | PASS | init 353.3 ms (attempt 1) |
| capture_SVGA | PASS | 20/20 valid 800x600 JPEGs, wait avg 41.8 ms, 11810 bytes avg, 4 buffer(s) held 2+ pictures and were cut to the first |
| fail_stopped | PASS | capture with camera stopped returned no frame, handled |
| fail_init | PASS | start refused: ESP_FAIL (0xffffffff) after 315.1 ms, largest internal block 270336 < needed 629145: frame buffer refused, as expected |
| recover | PASS | camera started again after the failed start, frame valid 320x240 (2217 bytes) |
| memory_back | PASS | after all tests: internal 392 bytes, PSRAM 0 bytes less than before the camera started (limit 4096) |

| Resolution | Valid / shots | Init ms | Wait ms min/avg/max | FPS | Bytes min/avg/max | Lighting |
| --- | --- | --- | --- | --- | --- | --- |
| QVGA 320x240 | 20/20 | 348.0 | 61.1/70.9/72.2 | 13.9 | 2175/2215/2265 | lens covered |
| VGA 640x480 | 20/20 | 352.3 | 70.7/72.9/107.2 | 13.5 | 7200/7525/7979 | lens covered |
| SVGA 800x600 | 20/20 | 353.3 | 33.4/41.8/71.8 | 22.9 | 11126/11810/13548 | lens covered |

| Memory snapshot | Internal free | Internal largest block | PSRAM free | Lowest free ever |
| --- | --- | --- | --- | --- |
| before_camera | 364555 | 270336 | 8386192 | 8718716 |
| running_QVGA | 342215 | 262144 | 8369624 | 8676796 |
| running_VGA | 342215 | 262144 | 8323544 | 8623484 |
| running_SVGA | 342151 | 262144 | 8192924 | 8503044 |
| after_capture | 364163 | 270336 | 8386192 | 8503044 |
| after_all_tests | 364163 | 270336 | 8386192 | 8503044 |

Firmware `16ad91a-dirty`, ESP-IDF `v5.5.5`, built Nov 25 2026 00:40:00

| Image file | Size | Bytes | Decoded | Check |
| --- | --- | --- | --- | --- |
| 01_QVGA_shot01.jpg | 320x240 | 2210 | yes (re-checked with Pillow 12.2.0 on 2026-11-25) | OK |
| 02_VGA_shot01.jpg | 640x480 | 7578 | yes (re-checked with Pillow 12.2.0 on 2026-11-25) | OK |

Wrote 2 image(s) to photos/. Open them and check they show the scene.
Verdict: one complete run, all checks passed, all photos OK
