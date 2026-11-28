## Camera test log: camera_log_raw.txt

Result line: `CAM RESULT pass=13 fail=0`

| Check | Status | Details |
| --- | --- | --- |
| psram | PASS | 8192 KB |
| startup | PASS | init 352.0 ms (attempt 1) |
| sensor | PASS | OV3660 PID=0x3660 VER=0x00 MIDH=0x00 MIDL=0x00 (expected OV3660) |
| start_QVGA | PASS | init 348.0 ms (attempt 1) |
| capture_QVGA | PASS | 20/20 valid 320x240 JPEGs, wait avg 70.3 ms, 4253 bytes avg |
| start_VGA | PASS | init 351.8 ms (attempt 1) |
| capture_VGA | PASS | 20/20 valid 640x480 JPEGs, wait avg 69.5 ms, 12585 bytes avg |
| start_SVGA | PASS | init 348.3 ms (attempt 1) |
| capture_SVGA | PASS | 20/20 valid 800x600 JPEGs, wait avg 35.8 ms, 18758 bytes avg |
| fail_stopped | PASS | capture with camera stopped returned no frame, handled |
| fail_init | PASS | start refused: ESP_FAIL (0xffffffff) after 322.2 ms, largest internal block 262144 < needed 629145: frame buffer refused, as expected |
| recover | PASS | camera started again after the failed start, frame valid 320x240 (4259 bytes) |
| memory_back | PASS | after all tests: internal 480 bytes, PSRAM 0 bytes less than before the camera started (limit 4096) |

| Resolution | Valid / shots | Init ms | Wait ms min/avg/max | FPS | Bytes min/avg/max | Lighting |
| --- | --- | --- | --- | --- | --- | --- |
| QVGA 320x240 | 20/20 | 348.0 | 51.4/70.3/72.4 | 13.9 | 4201/4253/4297 | bright room |
| VGA 640x480 | 20/20 | 351.8 | 48.8/69.5/73.1 | 13.9 | 12481/12585/12694 | bright room |
| SVGA 800x600 | 20/20 | 348.3 | 33.6/35.8/70.0 | 26.4 | 18637/18758/18993 | bright room |

| Memory snapshot | Internal free | Internal largest block | PSRAM free | Lowest free ever |
| --- | --- | --- | --- | --- |
| before_camera | 361835 | 262144 | 8386192 | 8715996 |
| running_QVGA | 339495 | 262144 | 8369624 | 8671324 |
| running_VGA | 339343 | 262144 | 8323544 | 8613444 |
| running_SVGA | 339343 | 262144 | 8192924 | 8500236 |
| after_capture | 361355 | 262144 | 8386192 | 8500236 |
| after_all_tests | 361355 | 262144 | 8386192 | 8500236 |

Firmware `16ad91a-dirty`, ESP-IDF `v5.5.5`, built Nov 24 2026 23:07:36

| Image file | Size | Bytes | Decoded | Check |
| --- | --- | --- | --- | --- |
| 01_QVGA_shot01.jpg | 320x240 | 4276 | yes (re-checked with Pillow 12.2.0 on 2026-11-25) | OK |
| 02_VGA_shot01.jpg | 640x480 | 12694 | yes (re-checked with Pillow 12.2.0 on 2026-11-25) | OK |

Wrote 2 image(s) to ../../5_TESTING/doc/camera/run8_bright_fixed/photos/. Open them and check they show the scene.
Verdict: one complete run, all checks passed, all photos OK
