# T-016 BLE bring-up (ESP-IDF): test cases and results

- Ticket: T-016 ECU-Flutter BLE bring-up (development). Full verification (latency, malformed messages,
  unauthorized peers) follows in T-017.
- Original code by Mariam (NimBLE-Arduino), moved to the ESP-IDF NimBLE host with the same UUIDs, frame format,
  transport rules and log lines.
- Firmware: [components/icvsp_ble](../../../1_XAIO_SW/code/components/icvsp_ble) in the ESP-IDF project
  `1_XAIO_SW/code`, started by [main/ble_main.cpp](../../../1_XAIO_SW/code/main/ble_main.cpp). Selected with the build
  preset **T-016 BLE test** (`sdkconfig.ble`, Bluetooth on only in this build).
- Run 3 (2026-11-25, nRF Connect, current firmware) retested only the wrong/short/early ACK case (TC-BLE-04).
- Run 1 (nRF Connect) used the first port, which sent a JSON placeholder. Run 2 uses the ROVO Flutter app and a
  **real signed report** (schema 2, Ed25519, the same format as `POST /reports`) made by
  [main/ble_main.cpp](../../../1_XAIO_SW/code/main/ble_main.cpp) with the board's own key from NVS. Only the
  hazard is a test one: a pothole with no position (`gps_valid` false) and a test time (build time + time since
  boot, not real time), because this build has no sensors or GNSS.
- App: branch `shahesta/flutter-ble-warning` (from `main` at `16ad91a`, not committed yet). Malak's BLE code from
  `feat/flutter-ble-durable-ack` adapted to the current report queue, a **local warning screen**
  (`mobile/lib/screens/ecu_warning_screen.dart`) and a connect button on the Devices tab. The warning says
  "Detected by your device. Not confirmed yet": the backend contract says the app must not show a device report
  as a confirmed hazard.
- Protocol: `firmware/ble/PROTOCOL.md` (branch `feat/ble-durable-nvs-queue`). Since run 2 the firmware follows it
  on fragment size (MTU - 7, up to 240 bytes) and refuses wrong, early or badly sized ACKs with an ATT error.
- Out of scope: pairing or encryption (there is none on this link; any phone in range can connect).

All results below are from the **physical board** and a real phone.

## How to run it

1. VS Code with the ESP-IDF extension (ESP-IDF v5.5.x), open `1_XAIO_SW/code`.
2. `Cmd/Ctrl+Shift+P` > **ESP-IDF: Select Project Configuration** > **T-016 BLE test**.
3. Click **Build, Flash and Monitor** (flame icon). Wait for `BLE advertising as ICVSP-001, address ...`.
4. On the phone, in nRF Connect: Scanner > **ICVSP-001** > CONNECT > service `7b100001-...`.
5. Turn on notifications for `7b100002-...` (the report). Write ACKs to `7b100003-...` as a BYTE ARRAY: the
   transport ID as 2 bytes, big endian (`0001` for report 1).
6. Save everything the monitor printed to `ble_log_raw.txt`.

Command line instead of VS Code:

```bash
. ~/esp/esp-idf/export.sh
cd 1_XAIO_SW/code
idf.py -B build_ble -D SDKCONFIG=build_ble/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.ble" set-target esp32s3 build
idf.py -B build_ble -p PORT flash monitor
```

GATT service (same as the Arduino version and the Flutter app):

| Item | UUID | Use |
| --- | --- | --- |
| Service | `7b100001-6a34-4c82-9b51-2f048e9a6300` | ICVSP report service |
| Report | `7b100002-6a34-4c82-9b51-2f048e9a6300` | Notify: fragments `[transport ID hi][lo][index][count][16 bytes]` |
| Ack | `7b100003-6a34-4c82-9b51-2f048e9a6300` | Write: 2-byte transport ID, big endian |

## Test record

| Field | Run 1 | Run 2 |
| --- | --- | --- |
| Date | 2026-11-25 | 2026-11-25 |
| Tester | Shahesta | Shahesta |
| Setup | physical board XIAO ESP32-S3 Sense, ESP-IDF v5.5.5 NimBLE host, PSRAM 40 MHz | same board; board on the laptop for the log, phone on battery |
| Firmware | `firmware=16ad91a-dirty`: ESP-IDF port of `ble_app.cpp`, not committed yet | `firmware=16ad91a-dirty`: run 1 + signed reports, MTU-sized fragments, ATT errors on bad ACKs (not committed yet) |
| Board address | `68:ee:8f:4f:5f:fa` | `68:ee:8f:4f:5f:fa` |
| Phone | Xiaomi M2012K11AG, Android 15 | Xiaomi M2012K11AG, crDroid (Android 15), airplane mode with Bluetooth on (no Internet) |
| Scan app | nRF Connect for Mobile 4.29.1 | ROVO app, branch `shahesta/flutter-ble-warning` (debug build), `flutter_blue_plus` 1.36.8 |
| Security | none: no pairing, no encryption, any phone in range can connect | none (same as run 1) |
| Log | [run1_phone_nrf/ble_log_raw.txt](run1_phone_nrf/ble_log_raw.txt) (unedited serial output, from boot) | [run2_flutter_offline/ble_log_raw.txt](run2_flutter_offline/ble_log_raw.txt) (unedited; lines 1-106 are earlier boots while the phone could not connect, note 3) and [first_signed_report.json](run2_flutter_offline/first_signed_report.json) |

## Test cases

| ID | Check | Expected | Result | Status | Evidence |
| --- | --- | --- | --- | --- | --- |
| TC-BLE-01 | Advertising on boot | `BLE advertising as ICVSP-001, address ...` | Steps 1-3 of the BLE start, `BLE advertising as ICVSP-001, address 68:ee:8f:4f:5f:fa`; the phone found it by name in nRF Connect | Pass | lines 26-35 |
| TC-BLE-02 | Subscribe starts a fragmented report | `BLE central subscribed`, `BLE new report transport_id=1`, fragments 1/4 to 4/4 | Connected, subscribed, `transport_id=1 fragments=4 bytes=51`, fragments 1/4 to 4/4 | Pass | lines 37-43 |
| TC-BLE-03 | Correct ACK clears the report and starts the next | `BLE ACK accepted transport_id=N`, then report N+1 | 3 of 3: ACK `0001`, `0002`, `0003` accepted, then reports 2, 3 and 4 started. Each ACK came after the stall, so this also shows a late ACK is still accepted | Pass | lines 77-78, 116-117, 133-134 |
| TC-BLE-04 | Wrong or garbled ACK is ignored | `BLE WARN ... ignored`, nothing else changes | `0063` (wrong ID): `ACK id=99 does not match`; `01` (1 byte): `bad length=1 (want 2)`. Old IDs written again while report 4 was in flight (`0002`, `0003`, `0003`): all ignored. Retry and stall carried on unchanged | Pass | lines 75-76, 144, 151, 166 |
| TC-BLE-04 (run 3 retest, current firmware) | Wrong, short or early ACK is refused with an ATT error | Board logs `refused`, report is not marked delivered, the phone write fails | Board side: `0063` refused (`ACK id=99 does not match`), `01` refused (`bad length=1 (want 2)`), `0001` accepted, `0002`/`0003` written between reports refused (early ACK). No crash, report 2 started normally. Phone side: nRF Connect showed no clear error message, so the ATT error is not confirmed from the phone | Pass (board side); phone-side error not confirmed | run3_nrf_wrong_ack/ble_log_raw.txt lines 135, 181-184 |
| TC-BLE-05 | No ACK: retry, retry, stall | Retry at 2 s (`attempt 2/3`), again (`3/3`), then `stalled after 3 attempts` | Exactly that for report 1 and every later report left without ACK | Pass | lines 43-54 |
| TC-BLE-06 | Unsubscribe and resubscribe before ACK resumes the same report | Same transport ID again from fragment 1 | Notifications off then on: `unsubscribed`, `subscribed`, `restarting unacknowledged report transport_id=4 from fragment 1 (fresh attempts)` | Pass | lines 173-175 |
| TC-BLE-07 | Disconnect, reconnect, subscribe | Unacknowledged report resumes with the same ID; a new ID only after an ACK | Phone DISCONNECT (reason 0x213), reconnect, subscribe: report 4 (unacknowledged) resumed from fragment 1. Also after the 3 link drops (reason 0x208) the unacknowledged report resumed with the same ID; new IDs came only after ACKs | Pass | lines 153-156, 56-59, 95-100 |
| TC-BLE-08 | Metadata recorded | Phone, app, firmware, library versions, no pairing noted | Test record above, complete | Pass | — |
| TC-BLE-09 | Flutter app finds and connects (run 2) | App scans by the service UUID, connects, subscribes | Connected, MTU 256, subscribed. The first tries failed on the phone (Android GATT error 133, then a scan timeout); fixed in the app by waiting 0.6 s after the scan and trying the connect up to 3 times (note 3) | Pass (after the app fix) | run2 lines 147-149 |
| TC-BLE-10 | Signed report delivered and acknowledged by the app | 3 fragments of 240 bytes at MTU 256; app saves the report, then ACKs | 7 of 7 reports (683 bytes, 3 fragments each) acknowledged on attempt 1, no retry, no `notify failed`. On the laptop the first report passes the backend's own signature check (`backend_sw/icvsp_backend/verify.py`) and the event matches `engine/schema/safety_event.schema.json` | Pass | run2 lines 150-197, `first_signed_report.json` |
| TC-BLE-11 | Local warning with no Internet | Phone in airplane mode, Bluetooth on: "Pothole detected" warning for each new report | Warning shown for each report, about 10 s apart (seen by the tester; screenshots in run2_flutter_offline/). Nothing on that screen needs the network | Pass | run2_flutter_offline/warning_screen_airplane_mode.png, devices_2/3/4_reports.png |
| TC-BLE-12 | Disconnect and reconnect from the app | Disconnect, then Connect again: next report delivered | Disconnect (reason 0x213), board advertised again, the app connected and subscribed again, report 7 acknowledged | Pass | run2 lines 187-197 |

Run 1: 96 fragments sent, 0 `notify failed`, no crash or reboot (no `Guru Meditation` or `abort`).
Run 2: 21 fragments, 0 `notify failed`, 0 retries, no crash or reboot.

## Notes

1. **The phone dropped the link 3 times on its own** (reason 0x208, supervision timeout: the board stopped
   hearing the phone), once with nRF Connect reconnecting by itself. The board handled each drop correctly
   (advertising again, same report resumed). The cause was not measured. It is on the phone side or the radio path; possibly the phone
   saving power while the screen was off. To watch for in T-017 with the Flutter app.
2. **Known gaps.** Fixed since run 1: fragment size follows the MTU (run 2: 240 bytes), and a wrong, early or
   badly sized ACK now gets an ATT error. Run 3 retested this with nRF Connect: the board refuses each bad ACK (TC-BLE-04 run 3), but nRF showed no clear error, so the phone-side ATT error is not confirmed. Before that, TC-BLE-04 (run 1) was
   run when the write still succeeded. Still open:
   - no pairing, bonding or owner check;
   - the unacknowledged report is kept in RAM only (PROTOCOL.md asks for an NVS queue of 8);
   - the test report's time is not real (no GNSS in this build), so the backend would refuse it after 48 h.
3. **Android error 133 and scan timeout (fixed in the app).** With the first app build the phone found the board
   but the connect failed with GATT error 133, a generic Android Bluetooth error; the next try timed out while
   scanning (Android ignores an app's scans for about 30 s after several quick tries). The board saw no connection
   in either case. The app now waits 0.6 s after the scan and tries the connect up to 3 times; after that it
   connected first time.

## Next steps

1. Optional: confirm the ATT error on the phone (nRF Connect Log tab after a wrong write).
2. T-017: measured ECU-to-display latency, malformed messages, unauthorized-peer handling, app state.
