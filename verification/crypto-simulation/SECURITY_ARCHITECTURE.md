# Security boundaries — HTTPS/TLS vs Ed25519 (ROVO standalone simulation)

**These are distinct protections, not three layers of message encryption.** In the v2 demo the hazard payload stays readable. XChaCha20-Poly1305 (and its symmetric key, nonce, and tag) has been removed.

| Property | Ed25519 packet signature | HTTPS/TLS transport |
|---|---|---|
| Purpose | Authenticates the signing ECU key and detects changes to signed bytes | Protects confidentiality and integrity of the **phone → HTTPS server** connection |
| Where applied | ECU signs before BLE; Cloud verifies upon receipt | Phone establishes HTTPS connection to Cloud Run's HTTPS endpoint |
| Key at ECU | Device-specific **private Ed25519 signing key** (secret) | None for ordinary server-authenticated TLS uploads |
| Key at Cloud | **Trusted, registered Ed25519 public key** for that ECU (not secret) | TLS server private key/certificate managed by HTTPS endpoint/platform |
| Key at mobile | No Ed25519 signing private key required; can forward opaque bytes | Validates server's TLS certificate via OS trust store |
| What it does NOT do | Hide message contents or stop replay on its own | Protect BLE data or local phone storage; authenticate the physical ECU |

**Flow:** ECU creates `device_id + message_id + timestamp + lat/lon + hazard`, signs header and payload using Ed25519, sends over BLE to phone. The phone queues exact bytes offline. When online, it sends the same signed bytes over HTTPS/TLS to Cloud Run. Cloud matches the device ID to a **trusted registered public key**, verifies Ed25519, checks the fields (latitude, longitude, hazard type, timestamp format), then looks for an already accepted identical `(device_id, message_id, packet_hash)`. Only a report it has not seen yet gets the age and future-time checks, and is then stored.

**Who is authenticated:** only the ECU, through its signing key. The demo does not authenticate the phone or the user that uploads: anyone can send bytes to `POST /v1/hazards`, and only a correct signature from the registered key gets a report stored. Checking the uploading phone or user is a separate job for the real backend (user sign-in, FR-SEC-01).

**Key rule:** Signing uses the ECU's private key, verification uses the corresponding registered public key. Public-key trust must be established by secure enrollment; merely including a public key in a packet would not prove device identity. **No application-level symmetric encryption** is used in this revision.

**Replay/idempotency:** Signed bytes can be replayed. Exact valid retries already recorded get `duplicate` and are not stored twice, even after the 30-day acceptance window. New old packets are rejected. A different signed packet reusing the same device ID and Message ID gets `409`.

**Demo versus project:** The `ROVO` v2 120-byte packet, one demo device ID, `POST /v1/hazards` endpoint, and SQLite ledger are independent of the team's `firmware/lib/icvsp_core` signer implementation and `backend` `POST /reports`. No production interoperability is claimed. Local requests use HTTP / in-process TestClient, NOT TLS; actual TLS must be enforced and tested during the integration phase.
