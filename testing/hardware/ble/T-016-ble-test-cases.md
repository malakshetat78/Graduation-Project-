
# T-016 BLE bring-up: test cases and results

- Ticket: T-016 ECU-Flutter BLE bring-up (development). Verification follows in T-017.
- Procedure: [Arduino sketch guide, BLE test section](../../../xaio_sw/arduino/ICVSP_ECU/START_HERE.md#ble-test-mariam)
- Firmware: [ble_app.cpp](../../../xaio_sw/arduino/ICVSP_ECU/src/app/ble_app.cpp), selected with `#define ICVSP_APP ICVSP_APP_BLE` in [config.h](../../../xaio_sw/arduino/ICVSP_ECU/config.h)
- Out of scope: the real signed-report payload (still a JSON placeholder, see `make_simulated_report()`), the Flutter app's own warning screen, and pairing/encryption (there is none on this link; any phone in range can connect).

Every result below states its setup: **physical board**, **emulator** or **host**. Only physical-board results
count as BLE evidence.

## Test record (fill in for each board run)

| Field | Run 1 | Run 2 |
| --- | --- | --- |
| Date | 2026-11-24 | 2026-11-24 |
| Tester | Mariam | Shahesta (on her board, for Mariam) |
| Setup | physical board | physical board |
| Board | XIAO ESP32-S3 Sense | XIAO ESP32-S3 Sense |
| Tested commit / build | *(fill in: `git log -1 --oneline` on this branch, or "uncommitted working copy" if not committed yet)* | main `1cb6bb9` (`ble_app.cpp` unchanged; only `config.h` set to `ICVSP_APP_BLE`) |
| NimBLE-Arduino version | 2.5.2 | 2.5.2 |
| Arduino-ESP32 core version | *(fill in from Tools > Board, or the line at the top of the Verify/Upload output)* | 3.3.12 |
| Phone model + OS version | *(fill in, e.g. "Samsung A54, Android 14")* | *(fill in)* |
| Scan app + version | *(fill in, e.g. "nRF Connect for Mobile 4.28.1")* | nRF Connect for Mobile *(version: fill in)* |
| Security | none — no pairing, no encryption; any phone in range can connect | none ("CONNECTED, NOT BONDED" in nRF Connect) |
| Log file | [ble_test_run1.txt](run1/ble_test_run1.txt) (unedited Serial Monitor output) | [ble_test_run2.txt](run2/ble_test_run2.txt) (Serial Monitor output from the BLE start line on; the previous program's lines before it are cut) |
| Screenshots | *(add nRF Connect screenshots to `run1/screenshots/` and list them here)* | *(add to `run2/screenshots/` if taken)* |

## Test cases

| ID | Check | Steps (from START_HERE.md) | Expected | Actual | Pass/Fail | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| TC-BLE-01 | Advertising on boot | Upload, open Serial Monitor at 115200, press RESET | `BLE advertising as ICVSP-001, address ...` | Run 2: `BLE advertising as ICVSP-001, address 68:ee:8f:4f:5f:f9` after steps 1-3 of the BLE start | Pass | [run2 log](run2/ble_test_run2.txt) lines 1-8 |
| TC-BLE-02 | Subscribe starts a fragmented report | Connect with nRF Connect, enable notifications on `7b100002-...` | `BLE central subscribed`, `BLE new report transport_id=1`, then `fragment=1/4` through `4/4` | `BLE central subscribed, starting report transport` -> `BLE new report transport_id=1 fragments=4 bytes=52` -> fragments 1/4 to 4/4, attempt=1 | Pass | [run1 log](run1/ble_test_run1.txt) lines 4-9 |
| TC-BLE-03 | Correct ACK clears the transport and starts the next report | Write `0001` (hex) to `7b100003-...` right after fragment 4/4 | `BLE ACK accepted transport_id=1`, then report 2 starts | Run 2: ACK `0003` written while transport 3 was still being retried (attempt 2/3, before the stall): `BLE ACK accepted transport_id=3 (3 acknowledged so far)`, then `BLE new report transport_id=4` at once. Late ACKs after a stall were also accepted (transports 1 and 2), each starting the next report. An ACK with no retry at all needs a reply within 2 s, too fast by hand in nRF Connect; the Flutter app sends it automatically | Pass | [run2 log](run2/ble_test_run2.txt) lines 253-259 (also 202-204, 247-248) |
| TC-BLE-04 | Wrong/garbled ACK is ignored | Write a wrong transport ID (`0063`) or a 1-byte value (`01`) to `7b100003-...` | `BLE WARN ... ignored` line, nothing else changes | Run 2: wrong ID (`0001` while transport 2 was in flight): `BLE WARN ACK id=1 does not match in-flight transport_id=2, ignored`. 1-byte value `01`: `BLE WARN ACK write with bad length=1 (want 2), ignored`. Nothing else changed after either line (no ACK counted, no new report) | Pass | [run2 log](run2/ble_test_run2.txt) lines 223, 283 |
| TC-BLE-05 | No ACK -> retry, retry, stall | Do not ACK after a full fragment send | Retry at 2 s (`attempt 2/3`), retry again (`attempt 3/3`), then `stalled after 3 attempts` | Seen twice, matching exactly: transport 1 (lines 13-27) and transport 2 first pass (lines 52-66) | Pass | [run1 log](run1/ble_test_run1.txt) lines 13-27, 52-66; also run 2 (transports 1, 2, 4) [run2 log](run2/ble_test_run2.txt) lines 99-109, 210-221, 264-275 |
| TC-BLE-06 | Unsubscribe/resubscribe before ACK resumes the same report | Unsubscribe, then resubscribe, while a report is still unacknowledged | Same unacknowledged report resumes with the same transport ID, restarting from fragment 1 (the log counts fragments from 1) | `BLE restarting unacknowledged report transport_id=1 from fragment 1 (fresh attempts)` — same `transport_id`, not a new one, each time this happened (3 times total) | Pass | [run1 log](run1/ble_test_run1.txt) lines 10-12, 49-51, 67-69; also run 2 [run2 log](run2/ble_test_run2.txt) lines 91-93, 147-157 |
| TC-BLE-07 | Full disconnect -> reconnect -> subscribe | Disconnect on the phone, then reconnect and subscribe again | New transport ID only if the previous report was already ACKed; otherwise the same unacked report resumes | Run 1 confirmed the already-ACKed half. Run 2 confirmed the not-ACKed half twice: phone Bluetooth turned off with transport 1 unacknowledged, `BLE central disconnected reason=0x213, advertising again`, reconnect (the phone shows a new random address each time), subscribe: `BLE restarting unacknowledged report transport_id=1 from fragment 1` (same ID, not a new one) | Pass | [run1 log](run1/ble_test_run1.txt) lines 29, 36-39; [run2 log](run2/ble_test_run2.txt) lines 127-136, 175-186 |
| TC-BLE-08 | Metadata recorded | Record phone/app/firmware/library versions, note the lack of pairing | See Test record table above | Run 2 column filled in except the phone model/OS and nRF Connect version | Partial | — |

## Next steps

1. Fill in the phone model/OS and the nRF Connect version for run 2 (TC-BLE-08), and add screenshots if any.
2. T-017 (verification) with the Flutter app: an ACK with no retry, sent by the app right after fragment 4/4.