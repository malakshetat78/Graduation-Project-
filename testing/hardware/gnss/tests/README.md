# GNSS software tests

Run from the sketch directory: `sh tests/run_gnss_tests.sh` (requires clang++).
These tests compile the real gnss_app.cpp and nmea.cpp with a fake Arduino
clock and serial ports; no board, upload, network, or GPS receiver is used.
Synthetic NMEA coordinates are test fixtures, not accuracy measurements.

Coverage: initial UART timeout, 2500/3500 ms boundaries, zeroed unusable
coordinates, explicit NO_FIX, bad checksum, partial-pair recovery, 32-bit
clock rollover, S/D injection, busy/skip commands, interrupted sentence
recovery, recovery timeout. Both disabled and enabled builds run, with
undefined-behavior checking.

## Optional serial simulation on a board

In config.h select ICVSP_APP_GNSS and set
CONFIG_ICVSP_GNSS_SOFTWARE_TESTS to 1. These settings are deliberately not
enabled in the delivered default config (which still selects IMU).
Build/upload only when ready, open USB Serial at 115200, obtain VALID_FIX,
then send S or D. Each suppresses input for 6000 ms:

- S keeps byte activity but suppresses parser updates: expect STALE.
- D drains/discards UART input without updating activity: expect DISCONNECTED.

Both keep draining UART so queued data from the simulated outage is discarded.
Recovery starts at a new sentence boundary, needs fresh RMC and GGA, and
allows 15000 ms for VALID_FIX. Logs report timeout PASS/FAIL and a final
PASS/FAIL for recovery. A test without a current fix is skipped. A command
while a test runs reports BUSY. A real absence of bytes during S can cause
DISCONNECTED and correctly fails the STALE expectation.

The native suite proves software behavior only; physical reception,
wiring, acquisition and positioning accuracy still require hardware tests.
