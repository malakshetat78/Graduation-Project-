# Flutter Task 8 — BLE durable receipt and backend upload

This change is a **reviewable integration**, not a claim of on-device BLE
success. ESP32 is not available yet. The BLE firmware counterpart lives in
`firmware/ble/PROTOCOL.md` on branch `feat/ble-durable-nvs-queue`.

## Pipeline
1. In **Devices**, tap **Connect ESP32 BLE**. App scans for the 128-bit ICVSP
   service, connects, discovers Report Data (Notify) and Report ACK (Write).
2. The assembler accepts header `transportId:u16 big-endian, index:u8,
   total:u8` and bounded opaque signed fragment data (up to 2048 bytes total).
   It handles duplicate/out-of-order parts and resets on reconnect.
3. Each completed report is inserted transactionally into SQLite, with the
   original bytes preserved. Exactly matching persisted reports are safe to ACK
   again. Conflicting report IDs, full queue, invalid messages, and persistence
   errors are **not ACKed**.
4. After durable local insert/deduplication, write the **two-byte transport ID**
   to Report ACK using write-with-response. This is unrelated to the backend
   HTTP ACK and does not mean a hazard is confirmed.
5. Upload the unchanged raw bytes to `POST /reports`; finish only on explicit
   `201 accepted` / `201 duplicate`. Retry 429, transient 5xx, timeout and
   unknown responses with persisted retry times and exponential backoff.
   Permanent `retryable:false` responses move to local rejection history.

## Commands

From `mobile` with Flutter installed:

```powershell
flutter pub get
flutter analyze
flutter test test/ble_report_assembler_test.dart test/ble_delivery_processor_test.dart test/report_queue_test.dart
```

`pubspec.lock` has not been refreshed in this review branch; update and commit
it after `flutter pub get` produces a consistent dependency resolution.

## Known limits and safety
- **No ESP32 hardware was available**; BLE discovery, GATT notifications and
  actual device ACK have not been validated end-to-end.
- The firmware demo currently has **no BLE bonding/pairing/owner authorization**.
  This prototype should **not** be used for field deployment. BLE device name
  is not authentication.
- The app's queue uses `device_id/event_id` uniqueness. It parses the envelope
  for metadata, but stores and sends the **unmodified original bytes**.
- On full local queue, new BLE reports are refused rather than deleting
  unacknowledged entries. Firmware retains and retries.
- The app can resume pending HTTP uploads on next app launch. Continuous uploads
  or BLE scanning while the OS has killed the app are **not implemented**.
  The ESP32 must retain unacknowledged reports.
- Old v1 SQLite rows previously marked sent/rejected may have their raw bytes
  erased. These rows cannot be proven identical on a BLE retransmission and
  will not be ACKed; do not silently treat them as exact duplicates.
- The 7-day local history is for debugging, not unlimited archival. The first
  queue lifetime remains 47h for potholes and 13d for speed bumps.
- BLE service is currently a single-device, foreground connection, manually
  initiated via Devices. Automatic reconnect/bonding, real device testing,
  and background scheduling are future work.
- This branch does **not** change hosted Supabase schema, PR 8 backend contract,
  or ESP32 firmware.
