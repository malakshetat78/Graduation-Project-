# ROVO Phase 2 — Offline Mobile Relay + Local Cloud Simulator

Builds **directly on your working Phase 1**. The **160-byte encrypted/signed packet format is unchanged**:

- **C++ ECU** generates `ROVO header(47) || ciphertext(33) || tag(16) || Ed25519 signature(64)`.
- **Python Mobile Simulator** stores the 160-byte opaque packet in a local SQLite outbox while offline, **without decrypting it**.
- When connectivity returns, Mobile posts the same bytes to a **local FastAPI server**.
- **Python Cloud Simulator** verifies Ed25519 *first*, decrypts XChaCha20-Poly1305, checks the timestamp, and stores accepted report IDs in SQLite.

This is a **local, laptop-only simulator**. It uses `http://127.0.0.1` to test the flow; it DOES NOT claim to provide actual internet or TLS. For production, Flutter→Cloud Run must use **HTTPS**. BLE, Flutter, Supabase, key provisioning and Cloud Run deployment are NOT yet implemented.

## Files

| File | Purpose |
|---|---|
| `src/ecu_sim.cpp` | Unchanged Phase 1 C++ ECU simulator |
| `generate_keys.py` | Demo-only symmetric + Ed25519 keys |
| `build.sh` | Compile the C++ simulator (now creates missing `vendor/`) |
| `verify_packet.py` | Unchanged Phase 1 verification/decryption function |
| `cloud_api.py` | Local FastAPI ingestion, signature verification, replay/idempotency checks, SQLite reports |
| `mobile_sim.py` | Offline SQLite outbox, online upload/ack/retry |
| `show_reports.py` | Inspect SQLite reports |
| `test_phase1.py` | Original Phase 1 tests |
| `test_phase2.py` | Phase 2 automated tests |
| `requirements.txt` | Python dependencies |

## Setup (Mac Terminal, from the extracted `ROVO_Phase2` folder)

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
python generate_keys.py   # only once; do NOT overwrite existing keys
./build.sh
```

If you want to reuse Phase 1 demo keys instead, copy the whole existing `keys` folder into the new Phase 2 folder **before** running the simulator; skip `generate_keys.py`. Do not publish or send real keys to anyone.

## Run — three terminals

**Terminal 1: start local Cloud Simulator** (from `ROVO_Phase2`, with environment activated)

```bash
python -m uvicorn cloud_api:app --host 127.0.0.1 --port 8000
```

Keep this terminal open. To confirm, visit `http://127.0.0.1:8000/healthz`.

**Terminal 2: ECU sends to phone while offline** (from same folder)

```bash
./build/ecu_sim
python mobile_sim.py receive packet.bin
python mobile_sim.py status
```

Expected: `pending: 1`. At this point, the packet is stored locally and **not uploaded**.

Try multiple distinct hazards (the C++ simulator creates fresh `message_id` and nonce each run):

```bash
./build/ecu_sim
python mobile_sim.py receive packet.bin
./build/ecu_sim
python mobile_sim.py receive packet.bin
python mobile_sim.py status
```

Expected `pending: 3` if you followed all commands above. You can also **stop Terminal 1** and the pending encrypted packets remain safely queued in `data/mobile_queue.sqlite3`.

**Terminal 3: phone regains Internet / local server reachable**

```bash
python mobile_sim.py sync
python mobile_sim.py status
python show_reports.py
```

Expected: Mobile prints Cloud ACKs; `pending` becomes 0; Cloud database shows stored unique reports. You can use Terminal 2 for these commands too.

**Simulate no internet:** Stop the API with Ctrl+C, create and receive a fresh packet, then run `python mobile_sim.py sync`. The simulator will say network unavailable and preserve the queued packet. Restart the API and run `sync` again.

## Test Phase 2, including replay and modified messages

```bash
python -m pytest -v test_phase2.py
```

Tests include: valid packet accepted, identical retry acknowledged without duplication, *different but validly signed packet with reused Message ID* rejected (409), modified nonce/ciphertext/tag/signature rejected, future/stale timestamps rejected, body length/content type errors, offline storage, failed-network retry, successful replay-safe delivery, rejected packet kept for diagnosis, and **optional real C++ → Python API integration** if `./build/ecu_sim` is present.

Tests use temporary keys and databases. The original Phase 1 tests are still available:

```bash
./build/ecu_sim
python verify_packet.py
python test_phase1.py
```

## Cloud API behavior

- `GET /healthz` — simple health check.
- `POST /v1/hazards` — 160 raw encrypted bytes, `Content-Type: application/octet-stream`.
- `201 {"status":"accepted", ...}` — new valid hazard saved.
- `200 {"status":"duplicate", ...}` — exact same authenticated packet already recorded; acknowledged as safe transport retry. **Not stored twice**.
- `409` — **different authenticated packet** using the same `(device_id, message_id)`; replay/conflict.
- `422` — invalid crypto, packet length, or timestamp.

**Offline policy:** the demo accepts reports up to 30 days old, and allows up to 5 minutes of future timestamp skew. This is a configurable policy, not a cryptographic guarantee: the ECU needs a reliable clock before this is suitable for production.

**Important limitations:**

- No real BLE, ESP32-S3 firmware, Flutter, TLS termination, Supabase, or Google Cloud Run yet.
- Current key files are demo-only, with a single registered device ID. Real deployment needs a server-side per-device key registry and private key provisioning/storage.
- Ed25519 default Monocypher EdDSA mode is not interchangeable with standard Ed25519; use the optional `monocypher-ed25519` source already compiled by `build.sh`.
- No mobile-authentication or per-device rate limiting at the API. The signature proves the packet was signed with a trusted key, not which phone uploaded it.
- The outbox SQLite DB is not OS-backed encrypted storage, and there are no background workers; those belong in the Flutter/production phase.
- Local `http://127.0.0.1` is exclusively for dev. Use **HTTPS/TLS** for real uploads.
- Some rejected packets are retained (marked `rejected`) for inspection, rather than being silently dropped or endlessly retried.
- Replay ledger data is stored locally (SQLite) and persists across server restarts, but production should use shared durable storage and apply retention policies.
