# Persistent boot identity and replay protection (prototype)

## Why

The existing ICVSP FastAPI backend validates Ed25519 signatures and enforces a
unique `device_id + boot_id + sequence` tuple for signed schema-v2 reports.
To avoid reusing that tuple after a restart, firmware must durably reserve a
fresh boot ID **before signing any live report**.

## Design

- `main/replay_identity.c`: portable pure-C state machine; no ESP-IDF or
  hardware calls. On a verified fresh store, reserves boot ID 1. On a
  subsequent boot, reads the stored boot ID, increments it, and **commits**
  the increment before allowing sequence 1. Each call to
  `icvsp_replay_next` yields one unique pair until sequence exhaustion.
- `main/replay_identity_esp.c`: storage adapter. With
  `CONFIG_ICVSP_ENABLE_SECURE_NVS=n` (the default), **live counter
  initialization fails closed**. It does not silently fall back to RAM.
- `main/secure_storage.c`: NVS namespace `anti_replay`, item `last_boot`,
  inside the existing `icvsp_keys` encrypted partition when secure storage
  has been explicitly provisioned. A commit failure, read failure,
  malformed stored counter (0 or maximum), or exhausted counter stops
  signing.

The existing historical `main.c` sample still signs an explicitly labeled
**DEMO** message with dummy GPS/time and a hardcoded event ID. It never
uploads a live report. Live sensor report construction must use the reserved
pair and form `event_id = device_id + '-' + boot_id + '-' + sequence`, then
sign schema-v2 event JSON with the existing `report_signer.c`.

## Local tests (no board)

From the repository root on Windows (MinGW gcc on PATH):

```bat
python firmware\espidf_security\tools\run_replay_identity_tests.py
```

Then use the ESP-IDF v5.5.5 environment:

```bat
cd firmware\espidf_security
set CMAKE_BUILD_PARALLEL_LEVEL=1
idf.py build
```

The tests simulate fresh storage, restart, failed write, failed read,
invalid/overflow boot ID, and sequence exhaustion. No eFuses are touched.

## Security limitations

This is a **logical replay-safety implementation**, not a production-ready
anti-rollback defense:

1. Default RAM-only key mode deliberately does not provide persistent
   identity, and live report signing is disabled in that mode.
2. HMAC-backed encrypted NVS needs real hardware provisioning and remains
   unverified on board. Never flash or burn eFuses on an unreviewed device.
3. **Encryption does not guarantee rollback resistance**: replacing or
   restoring an older complete flash image could restore an old boot ID.
   Reliable hardware/server backed anti-rollback identity policy is still
   required; protect against cloning and restore scenarios.
4. This module does not implement Flutter BLE transport, GNSS time acquisition,
   device enrollment, or the full live event builder. Existing backend
   duplicate and conflicting boot/sequence checks remain defense in depth.
5. Backend's current uniqueness constraint rejects duplicate tuples but
   cannot alone prove every new report's boot counter exceeds the last one.
   Strong monotonic-enforcement and resynchronization policy remain future
   work.
