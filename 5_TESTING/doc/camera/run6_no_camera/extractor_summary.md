## Camera test log: camera_log_raw.txt

Result line: `CAM RESULT pass=3 fail=4`

| Check | Status | Details |
| --- | --- | --- |
| psram | PASS | 8192 KB |
| startup | FAIL | camera did not start after 3 attempts: ESP_ERR_NOT_SUPPORTED (0x106). Unplug the board, reseat the camera ribbon, try again |
| capture | FAIL | skipped: the camera did not start |
| fail_stopped | PASS | capture with camera stopped returned no frame, handled |
| fail_init | FAIL | not run: needs a camera that starts (fix the startup check first) |
| recover | FAIL | not run: needs a camera that starts |
| memory_back | PASS | after all tests: internal 108 bytes, PSRAM 0 bytes less than before the camera started (limit 4096) |

| Memory snapshot | Internal free | Internal largest block | PSRAM free | Lowest free ever |
| --- | --- | --- | --- | --- |
| before_camera | 361835 | 262144 | 8386192 | 8715996 |
| after_capture | 361727 | 262144 | 8386192 | 8714152 |
| after_all_tests | 361727 | 262144 | 8386192 | 8714152 |

Firmware `16ad91a-dirty`, ESP-IDF `v5.5.5`, built Nov 24 2026 23:07:36

| Image file | Size | Bytes | Decoded | Check |
| --- | --- | --- | --- | --- |
| (no images in the log) | | | | |

RUN PROBLEM: the firmware reported 4 failed check(s)

RUN PROBLEM: FAIL lines: startup, capture, fail_init, recover

RUN PROBLEM: no photos in the log (set CONFIG_ICVSP_CAM_DUMP_JPEG to 1)

Wrote 0 image(s) to ../../5_TESTING/doc/camera/run6_no_camera/photos/. Open them and check they show the scene.
Verdict: NOT a complete passing run (see the problems above)
