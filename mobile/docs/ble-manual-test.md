# Manual BLE Connectivity Test — feat/flutter-ble-durable-ack

**Date:** 2026-11-15
**Tester:** Mariam
**Branch under test:** origin/feat/flutter-ble-durable-ack (Malak's BLE implementation)

## Setup
- Device A (central): real Android phone (SM A325F, Android 13), running the
  ROVO app built from eat/flutter-ble-durable-ack, connected via USB for logs.
- Device B (peripheral stand-in): a second real Android phone running
  **nRF Connect for Mobile**, configured as a custom GATT server with the
  exact UUIDs from lib/services/ble_service.dart:
  - Service: 7b100001-6a34-4c82-9b51-2f048e9a6300
  - Data (NOTIFY): 7b100002-6a34-4c82-9b51-2f048e9a6300
  - Ack (WRITE): 7b100003-6a34-4c82-9b51-2f048e9a6300
- No real ESP32 hardware was available yet, so Device B simulated it.

## Results

| Step | Result |
|---|---|
| Scan for device advertising the service UUID | Pass (after fixing an advertising-config issue, see Findings) |
| GATT connect + service/characteristic discovery | Pass — app correctly validated NOTIFY/WRITE properties |
| Receive a malformed BLE frame (too short) | Pass — app rejected it with a clear error instead of crashing |
| Receive a valid signed report over BLE (fragmented, header+JSON) | Pass — report stored locally and ACK written back automatically |
| Receive a valid report with an already-expired detected_at | Pass — correctly auto-rejected as expired |
| Receive a valid, current report | Pass — stored with state=queued, ready for upload |

## Findings / suggestions for Malak
1. The 'Connect ESP32 BLE' button correctly surfaces a clear
   'Bluetooth connection failed: TimeoutException ...' message when no
   peripheral is in range.
2. UX note: on a *successful* data receive, lastProblem is never cleared
   and 
otifyListeners() is not called from the success path in
   _handleFragment, so a previous error message can keep showing on
   screen even after a report is accepted. Might be worth clearing
   lastProblem and notifying on success too, for clearer manual testing/demo.
3. ReportQueue.store() requires a top-level sender field equal to
   event.device_id — this isn't documented anywhere else in the app code,
   worth a comment or README note for whoever writes the firmware side.

## Conclusion
Real BLE central-to-peripheral connectivity, data receipt, validation and
local persistence all work end-to-end on real hardware (two phones).
Task 'Try to connect application through BLUE' verified manually.
