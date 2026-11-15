# Screenshots

Rendered by `make_shots.py` from the committed logs in `../logs/`: every line shown is copied from a real log;
lines in grey italics are notes (omitted lines, run length), not device output. The badge says where it ran.

| Image | Where | What it shows |
|---|---|---|
| `1_laptop_tests.png` | HOST (laptop) | 91/91 unit tests of the device logic |
| `2_emulator_smoke_test.png` | EMULATOR (QEMU) | The ECU firmware boots; chip and signing PASS; sensors SKIP (not emulated) |
| `3_board_firmware_build.png` | BUILD | The ECU firmware builds for the XIAO ESP32-S3 with 0 warnings |
| `4_bug_found_and_fixed.png` | EMULATOR (QEMU) | Stack overflow + reboot loop in the signing demo, and the run with the fix |

Regenerate: `python3 make_shots.py "$PWD/../logs" "$PWD"` (needs a Chromium browser; path set in the script).
