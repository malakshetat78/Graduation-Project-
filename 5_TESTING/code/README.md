# 5_TESTING/code

Tests and tools that run on a computer, not on the board (they are not hardware evidence).

| Folder | What |
|---|---|
| [verification/](verification/) | requirements list, requirement -> test traceability, golden device reports, the CI check |
| [camera/](camera/README.md) | `extract_jpegs.py` turns a camera log into photos; `run_host_tests.sh` tests the JPEG check |
| [imu/](imu/test_accel_cal.cpp) | test for the 6-side accelerometer calibration |
| [gnss/](gnss/run_nmea_tests.sh) | 23 tests for the NMEA parser (`run_nmea_tests.sh`) |
