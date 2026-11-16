# ROVO Phase 3 — Signed Reports and Offline Delivery (Standalone Simulation)

**Scope:** Laptop-only demonstration. This directory is **NOT integrated** with the graduation project's production firmware signer (`firmware/lib/icvsp_core`, `signer.h`) or the actual backend `POST /reports` endpoint. It uses a separate `ROVO` v2 binary format and demo `/v1/hazards` endpoint. Integration and format reconciliation require a separate task and review **before production use**.

## Security architecture

- **ECU (C++ / Monocypher):** signs hazard data using **standard Ed25519** and the ECU's private signing key. It does **not encrypt** the message.
- **Flutter/mobile relay (simulated here in Python):** receives and queues the signed bytes offline, then forwards the same bytes when it has Internet. The mobile app **can read the data**; the signature alone is not confidential storage.
- **HTTPS/TLS:** protects the **mobile-to-cloud network connection** in a real deployment. TLS is **not** exercised by the local HTTP/TestClient simulation; HTTPS between phone and Cloud does **not** protect the BLE leg or locally saved data.
- **Cloud (Python):** uses the trusted ECU public signing key to verify the signature, then validates the hazard fields, applies offline age rules, and records the report. The Cloud does **not decrypt** the packet.
- See [SECURITY_ARCHITECTURE.md](SECURITY_ARCHITECTURE.md) for key ownership and trust boundaries.

## Wire packet v2: 120 bytes (standalone demo only)

| Field | Bytes | Details |
|---|---:|---|
| Magic | 4 | `ROVO` |
| Version | 1 | `0x02` |
| Device ID | 16 | `ROVO-DEMO-ECU-01` |
| Payload length | 2 | Big-endian; fixed 33 |
| Message ID | 16 | Random, unique per device |
| Timestamp | 8 | UTC epoch milliseconds, big-endian |
| Latitude, longitude | 4 + 4 | Signed int32, degrees × 1e7, big-endian |
| Hazard type | 1 | 1 = pothole, 2 = speed bump |
| Ed25519 signature | 64 | Sign **all preceding 56 bytes** |
| **Total** | **120** | Plaintext + signature; no nonce/tag/ciphertext |

**Ed25519 is a signature algorithm, NOT encryption.** The signature covers the header and payload, including device ID and timestamp. The Cloud verifies before trusting any payload values. The code uses Monocypher's optional **standard Ed25519** implementation for cross-language compatibility with Python `cryptography`.

## Files

| Path | Purpose |
|---|---|
| `src/ecu_sim.cpp` | Laptop C++ ECU generator / signer |
| `generate_keys.py` | Demo Ed25519 keypair (never upload keys/) |
| `build.sh` | Obtain checksum-verified Monocypher 4.0.3 and build |
| `verify_packet.py` | Verify Ed25519 and parse v2 packet |
| `cloud_api.py` | FastAPI demo endpoint, age validation, duplicate handling, SQLite |
| `mobile_sim.py` | Offline SQLite queue and resend logic |
| `conftest.py`, `test_support.py` | Isolated pytest fixture and independent signing helpers |
| `test_phase1.py`, `test_phase2.py`, `test_phase3.py` | Verification, resilience, security regression checks |
| `demo_phase3.py` | 10 native C++ packets → outbox → in-process FastAPI scenario |
| `show_reports.py` | Print locally stored demo reports |

## Install and run on macOS

Run these commands **inside `verification/crypto-simulation`** (not inside a downloaded Phase 2 ZIP):

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install -r requirements.txt
python generate_keys.py   # ONE TIME; don't overwrite demo signing keys
./build.sh
```

If you already have `keys/sign_seed.bin` and `keys/sign_pub.bin` from the old simulation, they remain usable; old `enc_key.bin` is **unused**. Never commit `keys/`, secrets, `data/`, `vendor/`, `.venv/`, or generated binaries. For genuine devices, use per-device secure key provisioning instead.

**Terminal 1** — run the Cloud locally (port 8001, deliberately not the main backend's 8000):

```bash
python -m uvicorn cloud_api:app --host 127.0.0.1 --port 8001
```

**Terminal 2** — send while offline, then sync:

```bash
./build/ecu_sim
python verify_packet.py
python mobile_sim.py receive packet.bin
python mobile_sim.py status
python mobile_sim.py sync
python show_reports.py
```

To prove offline behavior: stop Terminal 1 before `sync`. The app retains the queued packet; restart Terminal 1, then run `python mobile_sim.py sync` again. The transfer is over **local HTTP only**. Production requires HTTPS/TLS and a real authenticated endpoint.

**Run automated tests** (isolated temporary signing keys and SQLite databases):

```bash
python -m pytest -v test_phase1.py test_phase2.py test_phase3.py
python demo_phase3.py   # requires ./build.sh and local demo signing keys
```

The tests include valid signing and verification, tampering, unknown device (same valid key, different device ID), conflicting Message ID, stale/future timestamps, **a 31-day delayed identical retry after lost ACK**, startup-only DB initialization, ten offline packets, a lost ACK, and optional native C++ / Python compatibility.

## Endpoint / duplicate behavior

- `POST /v1/hazards` accepts exactly 120 bytes of `application/octet-stream` on this simulator; NOT the team's `POST /reports`.
- `201 accepted`: valid new signed report stored once.
- `200 duplicate`: an already stored **identical verified signed packet** retried at any age, even after the 30-day new-report window.
- `409 conflict`: a **different, validly signed** packet with an already used `(device_id, message_id)`.
- `422`: bad signature, unknown device, malformed packet, illegal hazard data, stale **new** report, or future timestamp.
- `413`: oversized payload; `415`: unsupported media type; `503`: database temporarily unavailable.

Signature verification happens **before** duplicate lookup, and lookup happens **before** the age check. The SQLite primary key also prevents duplicate rows on concurrent inserts. The 30-day offline age and 5-minute clock skew are demonstration policies, not device security guarantees. Database creation occurs during the FastAPI application **lifespan startup**, not on module import.

## Limitations and integration handoff

- No physical ESP32-S3, GNSS, Bluetooth LE, Flutter, deployed Cloud Run, production TLS, Supabase, device enrollment, or secure key storage tested.
- The demo hardcodes **one registered device ID** and loads a demo public key from a local file. Public keys need a trusted registry, private keys must never leave the device, and TLS certificate/private key handling belongs to the server/platform.
- An Ed25519 signature ensures source-key authenticity and integrity, **not confidentiality**. The phone can see and locally store the plaintext. BLE and storage require their own threat-model decisions.
- Do not replace `signer.h`, production signed-report format, or `POST /reports` using this simulator. Verify their existing data model and canonical signed bytes before integration.
- `test_phase1.py` is converted into pytest tests and does not need `packet.bin` at import time. Native C++ tests automatically skip when the simulator has not been built.
