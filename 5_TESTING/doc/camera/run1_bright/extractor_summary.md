## Camera test log: log.txt

Result line: `CAM RESULT pass=13 fail=0`

| Check | Status | Details |
| --- | --- | --- |
| psram | PASS | 8192 KB |
| startup | PASS | init 352.1 ms (attempt 1) |
| sensor | PASS | OV3660 PID=0x3660 VER=0x00 MIDH=0x00 MIDL=0x00 (expected OV3660) |
| start_QVGA | PASS | init 348.0 ms (attempt 1) |
| capture_QVGA | PASS | 20/20 valid 320x240 JPEGs, wait avg 70.6 ms, 4371 bytes avg |
| start_VGA | PASS | init 352.7 ms (attempt 1) |
| capture_VGA | PASS | 20/20 valid 640x480 JPEGs, wait avg 70.6 ms, 12543 bytes avg |
| start_SVGA | PASS | init 346.6 ms (attempt 1) |
| capture_SVGA | PASS | 20/20 valid 800x600 JPEGs, wait avg 91.5 ms, 21328 bytes avg |
| fail_stopped | PASS | capture with camera stopped returned no frame, handled |
| fail_init | PASS | start refused: ESP_FAIL (0xffffffff) after 317.7 ms, largest internal block 262144 < needed 629145: frame buffer refused, as expected |
| recover | PASS | camera started again after the failed start, frame valid 320x240 (4384 bytes) |
| memory_back | PASS | after all tests: internal 328 bytes, PSRAM 0 bytes less than before the camera started (limit 4096) |

| Resolution | Valid / shots | Init ms | Wait ms min/avg/max | FPS | Bytes min/avg/max | Lighting |
| --- | --- | --- | --- | --- | --- | --- |
| QVGA 320x240 | 20/20 | 348.0 | 51.6/70.6/72.1 | 13.9 | 4340/4371/4405 | bright room |
| VGA 640x480 | 20/20 | 352.7 | 50.5/70.6/72.0 | 13.9 | 12464/12543/12619 | bright room |
| SVGA 800x600 | 20/20 | 346.6 | 71.4/91.5/179.5 | 10.8 | 18141/21328/38730 | bright room |

| Memory snapshot | Internal free | Internal largest block | PSRAM free | Lowest free ever |
| --- | --- | --- | --- | --- |
| before_camera | 361835 | 262144 | 8386192 | 8715996 |
| running_QVGA | 339495 | 262144 | 8369624 | 8671196 |
| running_VGA | 339495 | 262144 | 8323544 | 8614108 |
| running_SVGA | 339495 | 262144 | 8288984 | 8596448 |
| after_capture | 361507 | 262144 | 8386192 | 8596448 |
| after_all_tests | 361507 | 262144 | 8386192 | 8596448 |

Firmware `16ad91a-dirty`, ESP-IDF `v5.5.5`, built Nov 24 2026 22:48:29

| Image file | Size | Bytes | Decoded | Check |
| --- | --- | --- | --- | --- |
| 01_QVGA_shot01.jpg | 320x240 | 4358 | yes | OK |
| 02_VGA_shot01.jpg | 640x480 | 12619 | yes | OK |

Wrote 2 image(s) to /private/tmp/claude-501/-Users-shahy-Desktop-ICSVP---NEW-AND-IMPROVED/efc16d64-bf82-417c-9f79-04b3247ecc44/scratchpad/espidf_cam_run1_out/photos/. Open them and check they show the scene.
Verdict: one complete run, all checks passed, all photos OK
