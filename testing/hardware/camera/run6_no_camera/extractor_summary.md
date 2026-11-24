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
| memory_back | PASS | after all tests: internal 356 bytes, PSRAM 0 bytes less than before the camera started (limit 4096) |

| Memory snapshot | Internal free | Internal largest block | PSRAM free | Lowest free ever |
| --- | --- | --- | --- | --- |
| before_camera | 336188 | 278516 | 8386096 | 8717016 |
| after_capture | 335832 | 278516 | 8386096 | 8714720 |
| after_all_tests | 335832 | 278516 | 8386096 | 8714720 |

Firmware `arduino-3.3.12`, ESP-IDF `v5.5.5`, built Nov 24 2026 18:29:57

| Image file | Size | Bytes | Decoded | Check |
| --- | --- | --- | --- | --- |
| (no images in the log) | | | | |

RUN PROBLEM: the firmware reported 4 failed check(s)

RUN PROBLEM: FAIL lines: startup, capture, fail_init, recover

RUN PROBLEM: no photos in the log (set CONFIG_ICVSP_CAM_DUMP_JPEG to 1)

Wrote 0 image(s) to photos/. Open them and check they show the scene.
Verdict: NOT a complete passing run (see the problems above)
