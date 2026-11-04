# ICVSP bench transport v0.1

This document is the proposed shared contract for firmware and Flutter. It is NOT
an assertion that the existing firmware v2 signed schema has been implemented.

## GATT
| Resource | UUID | Operation |
|---|---|---|
| Service | 7b100001-6a34-4c82-9b51-2f048e9a6300 | Primary |
| Report Data | 7b100002-6a34-4c82-9b51-2f048e9a6300 | Notify only |
| Report ACK | 7b100003-6a34-4c82-9b51-2f048e9a6300 | Write with response |

NimBLE supplies the Report Data CCCD (0x2902). Subscribe using the client library;
do not manually add another CCCD in firmware. Scan by service UUID; the bench
name ICVSP-001 is not a secure identity and is not unique across boards.

## Notification frame
| Offset | Size | Meaning |
|---|---|---|
| 0 | 2 bytes | transport ID, unsigned big endian, starts at 1 |
| 2 | 1 byte | fragment index, zero based |
| 3 | 1 byte | total fragments |
| 4 | remaining | opaque slice of the original signed report bytes |

Chunk bytes = min(negotiated ATT MTU - 3, 244) - 4.
At MTU 23: 16 report bytes, so 706 bytes require 45 fragments.
At MTU 185: 178 report bytes, so 706 bytes require 4 fragments.
At MTU 247+: 240 report bytes, so 706 bytes require 3 fragments.
No serialization/re-signing occurs inside this transport. Max report size 2048.
Freeze chunk size at subscription; later MTU increases do not alter an active
message. On reconnect/resubscribe reset reassembly state and restart at fragment 0.

## ACK
Exactly 2 bytes, big endian transport ID. Example ID 1 = hex 00 01.
Receiver must save complete opaque bytes durably BEFORE writing ACK. ACK is byte
receipt only, not signature verification, backend acceptance or hazard consensus.
Write response confirms the peripheral processed the ACK write. Late matching ACKs
are accepted even after retry exhaustion. Wrong/early ACKs receive ATT error.

## Scheduling and bounds
One accepted notification per 30 ms host tick. Retry whole message 2 s after the
last accepted fragment when no ACK arrives. Three attempts per subscription/link
session, then retain and stall the head (log a warning). Reconnect/resubscribe
restarts attempts. This is stop-and-wait FIFO, not a throughput benchmark.
A transient notify enqueue failure does not advance the fragment cursor; the host
tries that fragment again on the next tick. It has no independent retry limit;
a disconnect or queue overflow can end that wait.

Eight retained reports in RAM, including in flight. A full transport queue now
rejects new admission without evicting unacknowledged reports; the ingress task
retains its head until transport space opens. The separate four-entry FreeRTOS
ingress queue rejects new producer submissions with ESP_ERR_TIMEOUT when full;
the producer MUST retain/retry on that return code. Neither queue survives power
loss. These changes prevent silent RAM-queue eviction, NOT power-loss data loss. Transport IDs never wrap in a boot: after 65535 reports
new admission is rejected and logged. A new boot is a new session. Do not reuse
transport IDs as signed report IDs; backend idempotency uses the signed identity.

## Flutter implementation sequence
1. Discover service, connect, discover Report Data and Report ACK.
2. Subscribe to Report Data; clear partials on disconnect/resubscribe.
3. Parse header as specified. Bound partial count/length and expire incomplete
   reports (bench receiver uses 15 s); preserve identical parts across retries.
4. Join parts by index without changing the signed bytes.
5. Atomically persist complete report and pending-upload state, deduplicated by
   signed report identity or exact content hash, then ACK transport ID.
6. Upload unchanged signed envelope separately using the user's backend auth.
7. Retry transient network failures; quarantine permanent signature/schema rejects.
8. A repeated completed delivery must be ACKed again after durable deduplication.

## Security and scope
Bench prototype has no BLE pairing/bonding/owner authorization yet. A nearby client
could subscribe or spoof ACKs. Ed25519 signatures protect integrity, not secrecy or
transport ACK authorization. Add device ownership/enrollment and appropriate BLE
security before field use. The firmware does not implement signatures, sensors,
GPS, user auth, TLS, backend upload, freshness checks, or trust consensus.
