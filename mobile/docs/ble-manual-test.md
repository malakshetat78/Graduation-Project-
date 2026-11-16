# Manual BLE Connectivity Test — feat/flutter-ble-durable-ack

**Date:** 2026-11-15
**Tester:** Mariam
**Branch under test:** origin/feat/flutter-ble-durable-ack (Malak's BLE implementation)

## Setup
- Device A (central): real Android phone (SM A325F, Android 13), running the
  ROVO app built from feat/flutter-ble-durable-ack, connected via USB for logs.
- Device B (peripheral stand-in): a second real Android phone running
  **nRF Connect for Mobile**, configured as a custom GATT server with the
  exact UUIDs from lib/services/ble_service.dart:
  - Service: 7b100001-6a34-4c82-9b51-2f048e9a6300
  - Data (NOTIFY): 7b100002-6a34-4c82-9b51-2f048e9a6300
  - Ack (WRITE): 7b100003-6a34-4c82-9b51-2f048e9a6300
- No real ESP32 hardware was available yet, so Device B simulated it. The ECU's own BLE firmware was
  not part of this test.
- `lib/services/ble_service.dart` exists only on `feat/flutter-ble-durable-ack`, not on `main` yet.

## Results

| Step | Result |
|---|---|
| Scan for device advertising the service UUID | Pass (after fixing an advertising-config issue, see Findings) |
| GATT connect + service/characteristic discovery | Pass — app correctly validated NOTIFY/WRITE properties |
| Receive a malformed BLE frame (too short) | Pass — app rejected it with a clear error instead of crashing |
| Receive a correctly formatted report over BLE (fragmented, header+JSON) | Pass — report stored locally and ACK written back automatically. The app only checks that the envelope has the expected text fields, that `sender` equals `event.device_id`, and that `detected_at` parses; it does **not** check the Ed25519 signature; the backend checks that. The ACK means "the phone has stored it", not "the report is valid" |
| Receive a report whose detected_at is already past its lifetime | Pass — the app stores it as `queued` and ACKs it like any other new report (the ACK is sent for `stored` and `alreadyStored`; not for `conflict`, `full` or an error). The queue clean-up (`ReportQueue.cleanUp()`, at app start and when the uploader starts a run) later marks it `rejected` with result `expired`. If it arrives while an upload run is already going, it can still be uploaded before the next clean-up; the backend then judges its age |
| Receive a current, correctly formatted report | Pass — stored with state=queued, ready for upload |

## Findings / suggestions for Malak
1. The 'Connect ESP32 BLE' button correctly surfaces a clear
   'Bluetooth connection failed: TimeoutException ...' message when no
   peripheral is in range.
2. UX note: on a *successful* data receive, lastProblem is never cleared
   and notifyListeners() is not called from the success path in
   _handleFragment, so a previous error message can keep showing on
   screen even after a report is accepted. Might be worth clearing
   lastProblem and notifying on success too, for clearer manual testing/demo.
3. ReportQueue.store() requires a top-level sender field equal to
   event.device_id — this isn't documented anywhere else in the app code,
   worth a comment or README note for whoever writes the firmware side.

## Conclusion
Real BLE central-to-peripheral connectivity, data receipt, field validation and
local persistence work between two real phones, with nRF Connect standing in
for the ECU. Not covered here: a real ESP32 (XIAO ESP32-S3) as the peripheral,
and signature checks, which the backend does after upload.
Task 'Try to connect application through BLUE' verified manually.
