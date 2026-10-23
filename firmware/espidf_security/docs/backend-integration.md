# ESP32-S3 → Flutter BLE relay → existing ICVSP FastAPI backend

The actual backend is in `malakshetat78/Graduation-Project-` (`backend/`, branch `backend`). **Do not introduce a new /reports schema.**

## Existing backend protocol

Device-generated signed bytes:

```text
ASCII "ICVSP-v1" + NUL
sender UTF-8 + NUL
msg_id UTF-8 + NUL
utc_ms signed int64 big-endian
event JSON UTF-8 bytes exactly as transmitted
```

Backend `verify.py` extracts the raw event bytes from the JSON line. Required outer field order is **event first, sender second**; then `msg_id`, `utc_ms`, `public_key`, `signature` (signature is lower-case 128-character hex; public key lower-case 64-character hex). `schema_version=2`, `event.device_id=sender`, `event.event_id=msg_id`. The signature is verified with the **previously registered** device public key, never an untrusted key in a report. The phone forwards the entire signed JSON line unchanged using HTTPS POST /reports.

## Firmware implementation

- `main/report_signer.h/.c` builds this exact signature and envelope.
- `main/main.c` prints a clearly marked **historical demonstration event** for byte-contract inspection. It is **not a live hazard** and must never be posted as one.
- `tools/check_backend_contract.py` independently verifies one report line with Python `cryptography`, using the same signed-bytes contract as the real backend. For example, save just the JSON line between `DEMO_SIGNED_REPORT_START` and `DEMO_SIGNED_REPORT_END` as `report.json`, then execute `python tools/check_backend_contract.py report.json`.

## Actual backend acceptance

The real FastAPI endpoint already implements POST /devices (admin-authenticated registration), POST /reports, signature verification, schema validation, duplicate event handling and boot/sequence replay checks. Its test suite includes 21 API tests per repo README. Run its existing tests from its *own* checkout:

```bat
cd backend
python -m unittest -v
```

To test a live report end-to-end, register the public key with POST /devices using an admin credential (never commit this token), ensure a real current synchronized UTC time and sensor data, then POST the **unchanged device line**. A repeated identical line is idempotent (`duplicate`); altered signed bytes or a conflicting boot/sequence must be rejected.

## Safety gaps / next engineering tasks

- Current RAM-only keys are different every restart. Production requires provisioned encrypted persistence, device registration and key-rotation policy.
- The current hardcoded demonstration `boot_id=1, sequence=1` is **not** a safe monotonic-counter implementation. Real boot identity and sequence must be unique and persisted/derived safely before accepting live device reports.
- **No real GNSS acquisition, BLE transport, Flutter client, or remote backend call has been implemented in this ESP-IDF repo yet.**
- This new signed-message code has not yet passed a fresh ESP-IDF build in the user's environment and is not hardware-tested.
- Do not flash, burn eFuses, enable secure NVS, or merge until reviewed.
