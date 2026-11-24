# T-016 BLE bring-up: test cases and results

- Ticket: T-016 ECU-Flutter BLE bring-up (development). Verification follows in T-017.
- Procedure: [Arduino sketch guide, BLE test section](../../../xaio_sw/arduino/ICVSP_ECU/START_HERE.md#ble-test-mariam)
- Firmware: [ble_app.cpp](../../../xaio_sw/arduino/ICVSP_ECU/src/app/ble_app.cpp), selected with `#define ICVSP_APP ICVSP_APP_BLE` in [config.h](../../../xaio_sw/arduino/ICVSP_ECU/config.h)
- Out of scope: the real signed-report payload (still a JSON placeholder, see `make_simulated_report()`), the Flutter app's own warning screen, and pairing/encryption (there is none on this link; any phone in range can connect).

Every result below states its setup: **physical board**, **emulator** or **host**. Only physical-board results
count as BLE evidence.

## Test record (fill in for each board run)

| Field | Run 1 |
| --- | --- |
| Date | 2026-11-24 |
| Tester | Mariam |
| Setup | physical board |
| Board | XIAO ESP32-S3 Sense |
| Tested commit / build | *(fill in: `git log -1 --oneline` on this branch, or "uncommitted working copy" if not committed yet)* |
| NimBLE-Arduino version | 2.5.2 |
| Arduino-ESP32 core version | *(fill in from Tools > Board, or the line at the top of the Verify/Upload output)* |
| Phone model + OS version | *(fill in, e.g. "Samsung A54, Android 14")* |
| Scan app + version | *(fill in, e.g. "nRF Connect for Mobile 4.28.1")* |
| Security | none — no pairing, no encryption; any phone in range can connect |
| Log file | [ble_test_run1.txt](run1/ble_test_run1.txt) (unedited Serial Monitor output) |
| Screenshots | *(add nRF Connect screenshots to `run1/screenshots/` and list them here)* |

## Test cases

| ID | Check | Steps (from START_HERE.md) | Expected | Actual | Pass/Fail | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| TC-BLE-01 | Advertising on boot | Upload, open Serial Monitor at 115200, press RESET | `BLE advertising as ICVSP-001, address ...` | *(not inside the saved log window — the log starts from the first central connection; confirm this line in the next run by starting the capture before RESET)* | Pending | — |
| TC-BLE-02 | Subscribe starts a fragmented report | Connect with nRF Connect, enable notifications on `7b100002-...` | `BLE central subscribed`, `BLE new report transport_id=1`, then `fragment=1/4` through `4/4` | `BLE central subscribed, starting report transport` -> `BLE new report transport_id=1 fragments=4 bytes=52` -> fragments 1/4 to 4/4, attempt=1 | Pass | [run1 log](run1/ble_test_run1.txt) lines 4-9 |
| TC-BLE-03 | Correct ACK clears the transport and starts the next report | Write `0001` (hex) to `7b100003-...` right after fragment 4/4 | `BLE ACK accepted transport_id=1`, then report 2 starts | Only exercised as a **late** ACK (after the transport had already stalled and the phone had unsubscribed): `BLE ACK accepted transport_id=1 (1 acknowledged so far)`. A clean "ACK immediately after the last fragment, no retries" run was not captured separately. | Partial | [run1 log](run1/ble_test_run1.txt) line 29 |
| TC-BLE-04 | Wrong/garbled ACK is ignored | Write a wrong transport ID (`0063`) or a 1-byte value (`01`) to `7b100003-...` | `BLE WARN ... ignored` line, nothing else changes | Not exercised in this run | Pending | — |
| TC-BLE-05 | No ACK -> retry, retry, stall | Do not ACK after a full fragment send | Retry at 2 s (`attempt 2/3`), retry again (`attempt 3/3`), then `stalled after 3 attempts` | Seen twice, matching exactly: transport 1 (lines 13-27) and transport 2 first pass (lines 52-66) | Pass | [run1 log](run1/ble_test_run1.txt) lines 13-27, 52-66 |
| TC-BLE-06 | Unsubscribe/resubscribe before ACK resumes the same report | Unsubscribe, then resubscribe, while a report is still unacknowledged | Same unacknowledged report resumes with the same transport ID, restarting from fragment 0 | `BLE restarting unacknowledged report transport_id=1 from fragment 1 (fresh attempts)` — same `transport_id`, not a new one, each time this happened (3 times total) | Pass | [run1 log](run1/ble_test_run1.txt) lines 10-12, 49-51, 67-69 |
| TC-BLE-07 | Full disconnect -> reconnect -> subscribe | Disconnect on the phone, then reconnect and subscribe again | New transport ID only if the previous report was already ACKed; otherwise the same unacked report resumes | The "already ACKed" half is confirmed: after the ACK at line 29 (transport 1), the next subscribe (lines 37-39, same device re-connecting) correctly started a **new** report, transport_id=2. The "not yet ACKed, full disconnect (not just unsubscribe)" half has not been tried: TC-BLE-06 only covers unsubscribe/resubscribe while still connected. | Partial | [run1 log](run1/ble_test_run1.txt) lines 29, 36-39 |
| TC-BLE-08 | Metadata recorded | Record phone/app/firmware/library versions, note the lack of pairing | See Test record table above | Test record table filled in above (some fields still open) | Partial | — |

## Next steps to close out this run

1. Re-run with the Serial Monitor capture started **before** pressing RESET, to catch the `BLE advertising as
   ICVSP-001` startup line (TC-BLE-01).
2. Do a clean ACK test in isolation: subscribe, let all 4 fragments send once, ACK immediately — confirm
   `BLE ACK accepted` appears with no retries first, and that report 2 starts right after (TC-BLE-03).
3. Write a wrong-ID or wrong-length ACK on purpose and confirm the `WARN ... ignored` line (TC-BLE-04).
4. With an unacknowledged report in flight, fully disconnect the phone (its Bluetooth settings, not just
   nRF Connect's disconnect button), reconnect and subscribe again: confirm the same transport ID resumes
   from fragment 0, the same as TC-BLE-06's unsubscribe case (TC-BLE-07).
5. Fill in the still-open Test record fields (commit/build, ESP32 core version, phone model/OS, app version) and
   add the nRF Connect screenshots.