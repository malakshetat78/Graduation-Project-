# T-014 GNSS bring-up (ESP-IDF): test cases and results

- Ticket: T-014 GNSS bring-up (Seeed L76K). Original test code by Ahmed, moved to ESP-IDF.
- Firmware: [components/icvsp_gnss](../../../1_XAIO_SW/code/components/icvsp_gnss) in the ESP-IDF project
  `1_XAIO_SW/code`, started by [main/gnss_main.cpp](../../../1_XAIO_SW/code/main/gnss_main.cpp). Selected with the
  build preset **T-014 GNSS test** (`sdkconfig.gnss`). NMEA parsing: the shared parser
  [components/icvsp_board/nmea.cpp](../../../1_XAIO_SW/code/components/icvsp_board/nmea.cpp), the same one the ECU app uses.
- Wiring: L76K stacked on D6/D7, UART1 at 9600 baud. ESP32 TX GPIO43 (D6) -> module RX, ESP32 RX GPIO44 (D7)
  <- module TX.
- Out of scope: absolute position accuracy (GNSS-TC-05, needs a known reference point).

**Privacy:** the firmware prints `lat=hidden lon=hidden` by default (`ICVSP_GNSS_PRINT_COORDS` off). The runs below
were recorded with an earlier build that printed 2 decimals; those values were replaced with `lat=removed
lon=removed` before the logs were saved here. No log in this folder contains a position.

All results below are from the **physical board** and a real L76K module. The stale (S) and disconnect (D)
tests are **simulated in software**: the module stays connected and the firmware ignores its data for 6 s.
They test the ECU state handling, not a real cable or module loss. The real module removal is run 5
(GNSS-TC-06 and 07).

## How to run it

1. VS Code with the ESP-IDF extension (ESP-IDF v5.5.x), open `1_XAIO_SW/code`.
2. `Cmd/Ctrl+Shift+P` > **ESP-IDF: Select Project Configuration** > **T-014 GNSS test**.
3. Click **Build, Flash and Monitor** (flame icon). Take the board and laptop outside, antenna (flat square side)
   facing the sky. Indoors the module only reports NO_FIX.
4. Wait for `GNSS EVENT ... -> VALID_FIX` and check `quality=1` or `2` on the FIX lines (see below).
5. Stale test: type `S` in the monitor. Disconnect test: type `D`. Each hides the UART data in software for 6 s (the module stays connected) and then
   waits for a fresh fix. Wait for `GNSS TEST S PASS` / `GNSS TEST D PASS` before the next key.
6. Save everything the monitor printed to `gnss_log_raw.txt`.

Command line instead of VS Code:

```bash
. ~/esp/esp-idf/export.sh
cd 1_XAIO_SW/code
idf.py -B build_gnss -D SDKCONFIG=build_gnss/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.gnss" set-target esp32s3 build
idf.py -B build_gnss -p PORT flash monitor
```

What the lines mean:

| Line | Meaning |
| --- | --- |
| `GNSS EVENT <old> -> <new>` | State change: WAITING_UART, NO_VALID_NMEA, WAITING_RMC_GGA, NO_FIX, VALID_FIX, STALE, DISCONNECTED |
| `GNSS STATUS <state> sats=.. HDOP=.. quality=..` | Once a second while there is no usable position |
| `GNSS FIX lat=hidden lon=hidden UTC=.. sats=.. HDOP=.. quality=.. speed=..` | Once a second with a position |
| `GNSS SKY GP: in_view=.. with_signal=.. best_SNR=.. BD: ...` | Every 10 s: satellites the module sees (GP = GPS, BD = BeiDou), how many it hears, and the best signal in dB-Hz. 0 in view = antenna hears nothing |
| `GNSS MODULE TXT ANTENNA OK` | Message from the module itself |
| `GNSS DEBUG bytes=.. valid_NMEA=.. bad_checksum=.. overflow=..` | Every 10 s: UART health |
| `quality=` | From the GGA sentence: 0 no fix, 1 GPS fix, 2 differential fix, 6 **estimated** (dead reckoning, not measured) |

## Test record

All runs: 2026-11-24 (UTC), tester Shahesta, **physical board** XIAO ESP32-S3 Sense with the Seeed L76K and its
patch antenna, ESP-IDF v5.5.5, PSRAM at 40 MHz. `firmware=16ad91a-dirty`: the code was not committed yet.

| Run | Place | Firmware version | Result | Folder |
| --- | --- | --- | --- | --- |
| 1 | indoors, desk | A: first ESP-IDF port | NO_FIX, 0 satellites, UART clean | [run1_indoor_no_fix](run1_indoor_no_fix) |
| 2 | at a window, then outside | A | NO_FIX for the whole run (board time 133-560 s), 0 satellites | [run2_window_no_satellites](run2_window_no_satellites) |
| 3 | outside, after a new upload | B: A + `GNSS SKY` and `MODULE TXT` lines | VALID_FIX at quality 6 (estimated), S and D tests passed | [run3_outside_fix_S_D](run3_outside_fix_S_D) |
| 4 | outside, open sky | C: B + fixed parser (quality 1/2 only), coordinates hidden | VALID_FIX at quality 1, 7-10 satellites, HDOP 2.0-2.4; S and D tests passed; 2 estimated sentences correctly refused | [run4_open_sky_fixed_parser](run4_open_sky_fixed_parser) |
| 5 | indoor, GNSS board taken off and put back | C | Board without the module: DISCONNECTED after 5 s, no crash. Module put back (USB off): data and NO_FIX again | [run5_module_removed](run5_module_removed) |

In runs 2 and 3 the keys S and D were sent by a small recorder script on the laptop. Its own lines start with
`# recorder:`.

Firmware versions:

- **A**: the T-014 test moved to ESP-IDF (UART driver, key input over the USB port, same states and checks).
- **C**: B + the parser fix (problem 1) and coordinates hidden by default (`lat=hidden lon=hidden`).
- **B**: adds the `GNSS SKY` line (from the GSV sentences) and `GNSS MODULE TXT` lines, to tell a missing antenna
  from a weak sky. These lines are for diagnosis only; they do not change the fix logic.

## Test cases

| ID | What | Expected | Result | Status | Evidence |
| --- | --- | --- | --- | --- | --- |
| GNSS-TC-01 | No fix at the start | Valid NMEA arrives, state NO_FIX, no coordinates printed, no crash | Run 1: WAITING_UART -> WAITING_RMC_GGA -> NO_FIX, 380 valid sentences in 40 s, 0 bad checksums, 0 overflows. Run 2: NO_FIX for 7 min, 5712 valid sentences, 0 bad checksums | Pass | run1 lines 33-36, run2 last DEBUG line |
| GNSS-TC-02 | A real fix with live output | VALID_FIX with a measured position (quality 1 or 2), UTC time, satellites, HDOP | Run 4 (firmware C, open sky): 284 FIX lines over 5 min, all `quality=1`, 7-10 satellites, HDOP 2.0-2.4, UTC time on every line, 0 bad checksums in 4260 sentences. Run 3 (old parser) had only quality 6 and is not counted | Pass | run4 FIX lines 54-441 |
| GNSS-TC-03 | Stale data and recovery (S, simulated) | STALE during the 6 s, then a fresh VALID_FIX | Run 4: VALID_FIX -> STALE -> VALID_FIX, `GNSS TEST S PASS: fresh fix recovered` (on a quality 1 fix). Run 3 the same on a quality 6 fix | Pass | run4 lines 85-99 |
| GNSS-TC-04 | Lost UART and recovery (D, simulated) | DISCONNECTED during the 6 s, then a fresh VALID_FIX | Run 4, 2 of 2: VALID_FIX -> STALE -> DISCONNECTED -> STALE -> VALID_FIX, `GNSS TEST D PASS` both times (on a quality 1 fix) | Pass | run4 lines 125-141, 167-181 |
| GNSS-TC-05 | Position accuracy against a reference | — | Not evaluated: needs a surveyed reference point | Not run | — |
| GNSS-TC-06 | Real module loss (physical) | GNSS board off: state DISCONNECTED, no crash, no position | Pulling the board off while running restarted the XIAO (USB dropped for 0.3 s), so a loss during running could not be shown. After that restart, with no module: WAITING_UART -> DISCONNECTED after 4.9 s, `bytes=0` for over 2 minutes, no crash or reset, no position printed. Same again on a second start without the module | Pass (no module at start); loss while running not shown | `removed_and_reattached_log.txt` lines 156-167, 229-336 |
| GNSS-TC-07 | Module put back (physical) | Data again, then NO_FIX (indoors) | USB off, module put back, USB on. First try: still `bytes=0` (module not seated); after seating it again: 80 valid sentences in the first 10 s, 0 bad checksums, NO_FIX indoors | Pass (after reseating) | `reattached_data_back_log.txt` |

## Problems found

1. **The parser accepted an estimated position as a valid fix (fixed, checked on the board).** The
   shared NMEA parser counted any GGA quality above 0 as a fix. In run 3 the module reported quality 6, which
   means estimated (dead reckoning), and the parser called it VALID_FIX. The ECU app uses the same parser, so a
   report could have carried an estimated position as if it were measured. Fix in
   [nmea.cpp](../../../1_XAIO_SW/code/components/icvsp_board/nmea.cpp): only GGA quality 1 (GPS) and 2 (differential)
   count, and an RMC with mode `E` (estimated), `M` (manual), `S` (simulator) or `N` (no fix) gives no position.
   The time: it is first trusted only after a measured fix; after that, `S` and `M` sentences never change it
   (their time is typed or replayed, not the receiver's clock), while `E` and `N` still keep it running, as
   before. Four parser tests were added in `5_TESTING/code/device_logic/test/test_nmea/test_main.cpp` (run `5_TESTING/code/gnss/run_nmea_tests.sh`) (quality 6/7/8, modes E/M/S/N, S/M cannot
   change the time, and a differential fix that must still pass). On the laptop: 23 of 23 parser tests and 6 of 6
   ECU app tests pass, and the new tests fail on the old parser, so they catch these bugs. With the new parser, run 3
   would have shown NO_FIX, not VALID_FIX. On the board (run 4): the module sent 2 estimated sentences
   (`quality=6`) among 299 measured ones, and the state went VALID_FIX -> NO_FIX -> VALID_FIX for them
   (lines 53-58), so an estimated position was refused on real hardware too.
2. **Why the module stayed at quality 6 (closed: sky view).** In run 3 the module used 7 satellites with good signal
   (best 27-37 dB-Hz) but reported estimated positions with HDOP 14.4-14.8 for the whole 100 s (good is under 3).
   Ahmed's Arduino run had quality 1 and HDOP 2.8-3.0 with the same module. Likely causes: part of the sky blocked
   (buildings, trees) so the satellites were badly spread, or the run was too short after 9 minutes without
   satellites.
   Run 4, under open sky, had quality 1 with 7-10 satellites and HDOP 2.0-2.4 for 5 minutes (Ahmed's Arduino run:
   2.8-3.0). So run 3's quality 6 most likely came from the sky view at that spot rather than the module or the
   firmware (same module and firmware, only the place changed; not proven).
3. **No satellites at the window and for the first minutes outside (open).** Run 2 had 0 satellites for 7 min.
   Firmware A had no sky line, so it cannot show whether the antenna heard anything. Run 3, after the new upload,
   showed `ANTENNA OK` and 11 satellites in view within 11 s. The module kept its power during the upload, so it
   had likely been collecting satellite data in the background. Not proven; the `GNSS SKY` line now shows this
   directly if it happens again.

4. **Pulling the GNSS board off while running restarts the XIAO (open).** The module is stacked on the XIAO pins,
   so taking it off moves the board and dropped USB for 0.3 s; the XIAO then started again. The ECU cannot be shown
   losing the module while it keeps running this way; a wire on the module's TX line that can be cut would be
   needed. Putting it back needed two tries: the first time no data came (pins not fully in), the second worked.
   Take the module off and put it back only with USB unplugged.

## Next steps

1. GNSS-TC-05 (accuracy) needs a surveyed reference point; not planned for the bring-up.
