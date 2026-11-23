# T-014 GNSS Test Cases and Execution Results

The recorded run demonstrates GNSS fix acquisition, handling of an initial no-fix state, simulated stale data, and simulated receive disconnection with recovery. All four executed test cases below passed their stated checks. The complete supplied serial log is included unchanged in Appendix A.

## Test run details

| Item | Recorded value |
| --- | --- |
| Project | ICVSP ECU, T-014 GNSS bring-up |
| Board | XIAO ESP32-S3 (project configuration) |
| GNSS receiver | Seeed L76K (firmware startup identification) |
| GNSS UART | 9600 baud; ECU RX GPIO44 / D7, TX GPIO43 / D6 |
| Serial monitor | 115200 baud (project setting) |
| Firmware mode | GNSS; software tests enabled, as printed at startup |
| Execution date | 2026-11-23, from receiver UTC timestamps |
| Timestamped fix interval | 18:56:56–18:58:27 UTC; startup lines are not timestamped |
| Evidence | User-supplied `gps.txt`, reproduced in Appendix A |
| Firmware commit | Not recorded in the supplied log |
| Test environment | Physical location and sky conditions not recorded |

The S and D tests inject faults in software while the receiver is connected. They do not represent a physical cable-disconnection test. Steps below describe how to reproduce the recorded tests; outcomes are taken from the supplied log.

## Results overview

| Test case | Check | Execution result |
| --- | --- | --- |
| GNSS-TC-01 | Initial no-fix handling | PASS |
| GNSS-TC-02 | Acquisition and live fix output | PASS |
| GNSS-TC-03 | Simulated stale data and recovery with S | PASS, one run |
| GNSS-TC-04 | Simulated receive disconnection and recovery with D | PASS, two runs |
| GNSS-TC-05 | Absolute positioning accuracy against a reference | NOT EVALUATED |

## GNSS-TC-01 Initial no-fix handling

**Precondition:** GNSS firmware is running and the receiver has not yet acquired a usable fix.

**Steps:**

1. Start/reset the board with the GNSS receiver connected.
2. Capture startup events and status output before fix acquisition.

**Expected:** The app progresses through UART/NMEA initialization and reports `NO_FIX` when position messages have no usable fix. Status lines indicate no usable coordinates.

**Actual:** Startup transitions were `STARTING -> WAITING_UART -> NO_VALID_NMEA -> WAITING_RMC_GGA -> NO_FIX`. Repeated `NO_FIX` lines reported `sats=0`, `quality=0`, and `(no usable coordinates)`.

**Result:** PASS. This checks the initial no-fix state; loss of satellite fix after acquisition was not exercised in this capture.

## GNSS-TC-02 Acquisition and live fix output

**Precondition:** The receiver is connected and receiving sufficient satellite signals to acquire a fix.

**Steps:**

1. Allow the receiver to acquire a fix.
2. Observe the transition from `NO_FIX` to `VALID_FIX`.
3. Confirm periodic latitude, longitude, UTC, satellite, HDOP, quality, and speed output.

**Expected:** `VALID_FIX` is reported and usable fix fields are printed and updated.

**Actual:** `GNSS EVENT NO_FIX -> VALID_FIX` was followed by fix lines. The first recorded fix had UTC `2026-11-23T18:56:56Z`, four satellites, HDOP `3.00`, and quality `1`. Subsequent lines updated UTC and coordinates. Six satellites were reported during the S/D tests.

**Result:** PASS for acquisition and output. No reference coordinate was supplied, so this does not establish absolute positioning accuracy. The untimestamped startup prevents an exact time-to-first-fix measurement.

## GNSS-TC-03 Simulated stale data and recovery

**Preconditions:** `CONFIG_ICVSP_GNSS_SOFTWARE_TESTS=1`, GNSS mode selected, and current status `VALID_FIX`.

**Steps:**

1. Send uppercase `S` through the USB serial monitor.
2. Keep the receiver sending UART data while software suppresses parser updates for 6000 ms.
3. Observe stale status, followed by recovery after fresh RMC and GGA messages arrive.

**Expected:** `VALID_FIX -> STALE`, no usable coordinates reported while stale, timeout check `PASS`, then a fresh-fix recovery `PASS` and `VALID_FIX`.

**Actual evidence:**

```text
GNSS TEST S BEGIN software simulation 6000ms
GNSS EVENT VALID_FIX -> STALE
GNSS STATUS STALE sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS TEST timeout PASS; waiting for fresh RMC+GGA
GNSS TEST S PASS: fresh fix recovered
GNSS EVENT STALE -> VALID_FIX
```

The next recorded fix had UTC `2026-11-23T18:57:26Z`. No `DISCONNECTED` event appeared during this S run.

**Result:** PASS, one run.

## GNSS-TC-04 Simulated receive disconnection and recovery

**Preconditions:** Software tests enabled, GNSS mode selected, and current status `VALID_FIX`.

**Steps:**

1. Send uppercase `D` through the USB serial monitor.
2. Observe software discarding UART input without updating receive activity for 6000 ms.
3. Observe timeout and recovery events after fresh messages resume.
4. Wait for a valid fix and repeat the test.

**Expected:** `VALID_FIX -> STALE -> DISCONNECTED`, with no usable coordinates during stale/disconnected status. After injection ends, reception may first restore `STALE` while a complete fresh RMC/GGA pair is pending, followed by recovery `PASS` and `VALID_FIX`.

**Actual evidence from each of the two runs:**

```text
GNSS TEST D BEGIN software simulation 6000ms
GNSS EVENT VALID_FIX -> STALE
GNSS EVENT STALE -> DISCONNECTED
GNSS TEST timeout PASS; waiting for fresh RMC+GGA
GNSS EVENT DISCONNECTED -> STALE
GNSS TEST D PASS: fresh fix recovered
GNSS EVENT STALE -> VALID_FIX
```

The first fix recorded after each recovery had UTC `18:57:40Z` and `18:57:50Z`, respectively, on 2026-11-23.

**Result:** PASS, two runs.

## Timing and interpretation

The implemented freshness threshold is greater than 2500 ms since the last RMC or GGA update. The receive-disconnection threshold is greater than 3500 ms since the last accepted UART byte. Therefore, `STALE` before `DISCONNECTED` during D is expected. Fix output can continue briefly after an injection begins while the last position remains within its freshness window.

Status and test-event lines do not carry device timestamps in this capture, so exact detection latency cannot be independently measured from this log. Reported satellite count, HDOP, and quality can retain previous values during stale/disconnected states; they do not override the `(no usable coordinates)` status.

## GNSS-TC-05 Absolute positioning accuracy

**Status:** NOT EVALUATED.

**Required procedure:** Record GNSS fixes at a known reference location, identify the reference source and acceptance threshold, and calculate position error in metres. No reference measurement or accuracy acceptance threshold accompanies this log.

## Evidence index

Line numbers below refer to the original `gps.txt` including its initial blank lines, reproduced in Appendix A.

- Fix acquisition: line(s) 238.
- S injection start: line(s) 263.
- S recovery PASS: line(s) 272.
- D injection starts: line(s) 281, 299.
- D recovery PASS: line(s) 293, 310.

Original log SHA-256: `e13162bbe16d717678af8ebbd247327951da62977eb86f4a3b9a36124ea12451`.

## Appendix A Complete original serial log

The content inside the following block is the full supplied log, with no removed or edited lines.

```text


GNSS BEGIN T-014: Seeed L76K hardware bring-up
GNSS UART baud=9600 RX=GPIO44(D7) TX=GPIO43(D6)
GNSS INFO module TX -> ECU RX, module RX <- ECU TX; NO WIFI / NO CLOUD
GNSS INFO indoor NO_FIX is expected; test outdoors with clear sky
GNSS TEST enabled: S=stale, D=disconnect; start from VALID_FIX
GNSS EVENT STARTING -> WAITING_UART
GNSS STATUS WAITING_UART sats=0 HDOP=unknown quality=0 (no usable coordinates)
GNSS EVENT WAITING_UART -> NO_VALID_NMEA
GNSS EVENT NO_VALID_NMEA -> WAITING_RMC_GGA
GNSS EVENT WAITING_RMC_GGA -> NO_FIX
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS STATUS NO_FIX sats=0 HDOP=25.50 quality=0 (no usable coordinates)
GNSS EVENT NO_FIX -> VALID_FIX
GNSS FIX lat=30.5581790 lon=31.0039062 UTC=2026-11-23T18:56:56Z sats=4 HDOP=3.00 quality=1 speed=0.5km/h
GNSS FIX lat=30.5581788 lon=31.0039072 UTC=2026-11-23T18:56:57Z sats=4 HDOP=3.00 quality=1 speed=0.4km/h
GNSS FIX lat=30.5581800 lon=31.0039115 UTC=2026-11-23T18:56:58Z sats=4 HDOP=3.00 quality=1 speed=0.5km/h
GNSS FIX lat=30.5581807 lon=31.0039160 UTC=2026-11-23T18:56:59Z sats=4 HDOP=3.00 quality=1 speed=0.6km/h
GNSS FIX lat=30.5581783 lon=31.0039167 UTC=2026-11-23T18:57:00Z sats=4 HDOP=3.00 quality=1 speed=4.5km/h
GNSS FIX lat=30.5581885 lon=31.0039418 UTC=2026-11-23T18:57:01Z sats=5 HDOP=2.80 quality=1 speed=1.6km/h
GNSS FIX lat=30.5581885 lon=31.0039545 UTC=2026-11-23T18:57:02Z sats=5 HDOP=2.80 quality=1 speed=0.8km/h
GNSS FIX lat=30.5581848 lon=31.0039532 UTC=2026-11-23T18:57:03Z sats=5 HDOP=2.80 quality=1 speed=0.3km/h
GNSS FIX lat=30.5581817 lon=31.0039527 UTC=2026-11-23T18:57:04Z sats=6 HDOP=2.80 quality=1 speed=0.9km/h
GNSS FIX lat=30.5581752 lon=31.0039572 UTC=2026-11-23T18:57:05Z sats=6 HDOP=2.80 quality=1 speed=0.6km/h
GNSS FIX lat=30.5581752 lon=31.0039632 UTC=2026-11-23T18:57:06Z sats=6 HDOP=2.80 quality=1 speed=0.0km/h
GNSS FIX lat=30.5581717 lon=31.0039745 UTC=2026-11-23T18:57:07Z sats=6 HDOP=2.80 quality=1 speed=0.0km/h
GNSS FIX lat=30.5581710 lon=31.0039828 UTC=2026-11-23T18:57:08Z sats=6 HDOP=2.80 quality=1 speed=0.0km/h
GNSS FIX lat=30.5581770 lon=31.0039977 UTC=2026-11-23T18:57:09Z sats=6 HDOP=2.80 quality=1 speed=4.3km/h
GNSS FIX lat=30.5581748 lon=31.0039957 UTC=2026-11-23T18:57:10Z sats=6 HDOP=2.80 quality=1 speed=0.0km/h
GNSS FIX lat=30.5581775 lon=31.0039908 UTC=2026-11-23T18:57:11Z sats=6 HDOP=2.80 quality=1 speed=0.0km/h
GNSS FIX lat=30.5581765 lon=31.0039920 UTC=2026-11-23T18:57:12Z sats=6 HDOP=2.80 quality=1 speed=0.0km/h
GNSS FIX lat=30.5581690 lon=31.0039985 UTC=2026-11-23T18:57:13Z sats=6 HDOP=2.80 quality=1 speed=3.8km/h
GNSS FIX lat=30.5581627 lon=31.0040053 UTC=2026-11-23T18:57:14Z sats=6 HDOP=2.80 quality=1 speed=3.0km/h
GNSS FIX lat=30.5581592 lon=31.0040090 UTC=2026-11-23T18:57:15Z sats=6 HDOP=2.80 quality=1 speed=1.7km/h
GNSS FIX lat=30.5581567 lon=31.0040107 UTC=2026-11-23T18:57:16Z sats=6 HDOP=2.90 quality=1 speed=1.1km/h
GNSS FIX lat=30.5581552 lon=31.0040110 UTC=2026-11-23T18:57:17Z sats=6 HDOP=2.90 quality=1 speed=1.3km/h
GNSS FIX lat=30.5581517 lon=31.0040062 UTC=2026-11-23T18:57:18Z sats=6 HDOP=2.90 quality=1 speed=2.3km/h
GNSS FIX lat=30.5581558 lon=31.0040340 UTC=2026-11-23T18:57:19Z sats=6 HDOP=2.90 quality=1 speed=1.7km/h
GNSS TEST S BEGIN software simulation 6000ms
GNSS FIX lat=30.5581558 lon=31.0040340 UTC=2026-11-23T18:57:20Z sats=6 HDOP=2.90 quality=1 speed=1.7km/h
GNSS EVENT VALID_FIX -> STALE
GNSS STATUS STALE sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS STATUS STALE sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS STATUS STALE sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS STATUS STALE sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS STATUS STALE sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS TEST timeout PASS; waiting for fresh RMC+GGA
GNSS TEST S PASS: fresh fix recovered
GNSS EVENT STALE -> VALID_FIX
GNSS FIX lat=30.5581598 lon=31.0040278 UTC=2026-11-23T18:57:26Z sats=6 HDOP=2.90 quality=1 speed=1.2km/h
GNSS FIX lat=30.5581603 lon=31.0040337 UTC=2026-11-23T18:57:27Z sats=6 HDOP=2.90 quality=1 speed=2.2km/h
GNSS FIX lat=30.5581590 lon=31.0040497 UTC=2026-11-23T18:57:28Z sats=6 HDOP=2.90 quality=1 speed=6.0km/h
GNSS FIX lat=30.5581542 lon=31.0040652 UTC=2026-11-23T18:57:29Z sats=6 HDOP=2.90 quality=1 speed=6.0km/h
GNSS FIX lat=30.5581538 lon=31.0040810 UTC=2026-11-23T18:57:30Z sats=6 HDOP=2.90 quality=1 speed=5.4km/h
GNSS FIX lat=30.5581522 lon=31.0040825 UTC=2026-11-23T18:57:31Z sats=6 HDOP=2.90 quality=1 speed=2.7km/h
GNSS FIX lat=30.5581525 lon=31.0040897 UTC=2026-11-23T18:57:32Z sats=6 HDOP=2.90 quality=1 speed=2.8km/h
GNSS TEST D BEGIN software simulation 6000ms
GNSS FIX lat=30.5581525 lon=31.0040897 UTC=2026-11-23T18:57:33Z sats=6 HDOP=2.90 quality=1 speed=2.8km/h
GNSS FIX lat=30.5581525 lon=31.0040897 UTC=2026-11-23T18:57:34Z sats=6 HDOP=2.90 quality=1 speed=2.8km/h
GNSS EVENT VALID_FIX -> STALE
GNSS STATUS STALE sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS STATUS STALE sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS EVENT STALE -> DISCONNECTED
GNSS STATUS DISCONNECTED sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS STATUS DISCONNECTED sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS TEST timeout PASS; waiting for fresh RMC+GGA
GNSS EVENT DISCONNECTED -> STALE
GNSS STATUS STALE sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS TEST D PASS: fresh fix recovered
GNSS EVENT STALE -> VALID_FIX
GNSS FIX lat=30.5581712 lon=31.0040978 UTC=2026-11-23T18:57:40Z sats=6 HDOP=2.90 quality=1 speed=5.0km/h
GNSS FIX lat=30.5581760 lon=31.0041042 UTC=2026-11-23T18:57:41Z sats=6 HDOP=2.90 quality=1 speed=4.1km/h
GNSS FIX lat=30.5581802 lon=31.0041095 UTC=2026-11-23T18:57:42Z sats=6 HDOP=2.90 quality=1 speed=3.0km/h
GNSS FIX lat=30.5581860 lon=31.0041133 UTC=2026-11-23T18:57:43Z sats=6 HDOP=2.90 quality=1 speed=3.6km/h
GNSS TEST D BEGIN software simulation 6000ms
GNSS FIX lat=30.5581860 lon=31.0041133 UTC=2026-11-23T18:57:44Z sats=6 HDOP=2.90 quality=1 speed=3.6km/h
GNSS EVENT VALID_FIX -> STALE
GNSS STATUS STALE sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS STATUS STALE sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS EVENT STALE -> DISCONNECTED
GNSS STATUS DISCONNECTED sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS STATUS DISCONNECTED sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS STATUS DISCONNECTED sats=6 HDOP=2.90 quality=1 (no usable coordinates)
GNSS TEST timeout PASS; waiting for fresh RMC+GGA
GNSS EVENT DISCONNECTED -> STALE
GNSS TEST D PASS: fresh fix recovered
GNSS EVENT STALE -> VALID_FIX
GNSS FIX lat=30.5582030 lon=31.0041407 UTC=2026-11-23T18:57:50Z sats=6 HDOP=2.90 quality=1 speed=3.5km/h
GNSS FIX lat=30.5582030 lon=31.0041512 UTC=2026-11-23T18:57:51Z sats=6 HDOP=2.90 quality=1 speed=3.1km/h
GNSS FIX lat=30.5582013 lon=31.0041642 UTC=2026-11-23T18:57:52Z sats=6 HDOP=2.90 quality=1 speed=3.6km/h
GNSS FIX lat=30.5581988 lon=31.0041760 UTC=2026-11-23T18:57:53Z sats=6 HDOP=2.90 quality=1 speed=4.5km/h
GNSS FIX lat=30.5581952 lon=31.0041878 UTC=2026-11-23T18:57:54Z sats=6 HDOP=2.90 quality=1 speed=5.0km/h
GNSS FIX lat=30.5581923 lon=31.0042015 UTC=2026-11-23T18:57:55Z sats=6 HDOP=2.90 quality=1 speed=5.5km/h
GNSS FIX lat=30.5581892 lon=31.0042168 UTC=2026-11-23T18:57:56Z sats=6 HDOP=2.90 quality=1 speed=8.0km/h
GNSS FIX lat=30.5581945 lon=31.0042298 UTC=2026-11-23T18:57:57Z sats=6 HDOP=2.90 quality=1 speed=10.9km/h
GNSS FIX lat=30.5582023 lon=31.0042467 UTC=2026-11-23T18:57:58Z sats=6 HDOP=2.90 quality=1 speed=10.1km/h
GNSS FIX lat=30.5582085 lon=31.0042687 UTC=2026-11-23T18:57:59Z sats=6 HDOP=2.90 quality=1 speed=9.4km/h
GNSS FIX lat=30.5582125 lon=31.0042907 UTC=2026-11-23T18:58:00Z sats=6 HDOP=2.90 quality=1 speed=8.9km/h
GNSS FIX lat=30.5582122 lon=31.0043055 UTC=2026-11-23T18:58:01Z sats=6 HDOP=2.90 quality=1 speed=7.0km/h
GNSS FIX lat=30.5582045 lon=31.0043120 UTC=2026-11-23T18:58:02Z sats=6 HDOP=2.90 quality=1 speed=5.8km/h
GNSS FIX lat=30.5581943 lon=31.0042818 UTC=2026-11-23T18:58:03Z sats=6 HDOP=2.90 quality=1 speed=4.3km/h
GNSS FIX lat=30.5581952 lon=31.0042583 UTC=2026-11-23T18:58:04Z sats=6 HDOP=2.90 quality=1 speed=3.6km/h
GNSS FIX lat=30.5582070 lon=31.0042485 UTC=2026-11-23T18:58:05Z sats=6 HDOP=2.90 quality=1 speed=3.7km/h
GNSS FIX lat=30.5582208 lon=31.0042525 UTC=2026-11-23T18:58:06Z sats=6 HDOP=2.90 quality=1 speed=6.3km/h
GNSS FIX lat=30.5582365 lon=31.0044297 UTC=2026-11-23T18:58:07Z sats=6 HDOP=2.90 quality=1 speed=10.2km/h
GNSS FIX lat=30.5582425 lon=31.0044470 UTC=2026-11-23T18:58:08Z sats=6 HDOP=2.90 quality=1 speed=9.9km/h
GNSS FIX lat=30.5582467 lon=31.0044678 UTC=2026-11-23T18:58:09Z sats=6 HDOP=2.90 quality=1 speed=9.4km/h
GNSS FIX lat=30.5582468 lon=31.0044882 UTC=2026-11-23T18:58:10Z sats=6 HDOP=2.90 quality=1 speed=8.9km/h
GNSS FIX lat=30.5582423 lon=31.0044313 UTC=2026-11-23T18:58:11Z sats=6 HDOP=2.90 quality=1 speed=2.6km/h
GNSS FIX lat=30.5582418 lon=31.0044482 UTC=2026-11-23T18:58:12Z sats=6 HDOP=2.90 quality=1 speed=3.1km/h
GNSS FIX lat=30.5582400 lon=31.0044603 UTC=2026-11-23T18:58:13Z sats=6 HDOP=2.90 quality=1 speed=2.9km/h
GNSS FIX lat=30.5582328 lon=31.0044653 UTC=2026-11-23T18:58:14Z sats=6 HDOP=2.90 quality=1 speed=3.1km/h
GNSS FIX lat=30.5582253 lon=31.0044710 UTC=2026-11-23T18:58:15Z sats=6 HDOP=2.90 quality=1 speed=3.7km/h
GNSS FIX lat=30.5582175 lon=31.0044785 UTC=2026-11-23T18:58:16Z sats=6 HDOP=2.90 quality=1 speed=4.0km/h
GNSS FIX lat=30.5582098 lon=31.0044870 UTC=2026-11-23T18:58:17Z sats=6 HDOP=2.90 quality=1 speed=4.0km/h
GNSS FIX lat=30.5582015 lon=31.0044897 UTC=2026-11-23T18:58:18Z sats=6 HDOP=2.90 quality=1 speed=3.6km/h
GNSS FIX lat=30.5581940 lon=31.0044872 UTC=2026-11-23T18:58:19Z sats=6 HDOP=2.90 quality=1 speed=3.6km/h
GNSS FIX lat=30.5581853 lon=31.0044783 UTC=2026-11-23T18:58:20Z sats=6 HDOP=2.90 quality=1 speed=4.1km/h
GNSS FIX lat=30.5581772 lon=31.0044662 UTC=2026-11-23T18:58:21Z sats=6 HDOP=2.90 quality=1 speed=5.3km/h
GNSS FIX lat=30.5581658 lon=31.0044468 UTC=2026-11-23T18:58:22Z sats=6 HDOP=2.90 quality=1 speed=8.0km/h
GNSS FIX lat=30.5581502 lon=31.0044192 UTC=2026-11-23T18:58:23Z sats=6 HDOP=2.90 quality=1 speed=12.3km/h
GNSS FIX lat=30.5581330 lon=31.0043845 UTC=2026-11-23T18:58:24Z sats=6 HDOP=2.90 quality=1 speed=13.5km/h
GNSS FIX lat=30.5581138 lon=31.0043472 UTC=2026-11-23T18:58:25Z sats=6 HDOP=2.90 quality=1 speed=14.5km/h
GNSS FIX lat=30.5580907 lon=31.0043102 UTC=2026-11-23T18:58:26Z sats=5 HDOP=23.80 quality=1 speed=15.8km/h
GNSS FIX lat=30.5580632 lon=31.0042722 UTC=2026-11-23T18:58:27Z sats=5 HDOP=23.80 quality=1 speed=17.6km/h
```
