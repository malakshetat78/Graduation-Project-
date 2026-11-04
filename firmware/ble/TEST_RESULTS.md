# Verification — 2026-10-29

Executed on Linux, GCC 13.3, Python 3.12.14 with cryptography 50.0.2.

- Portable C state machine: PASS, compiled -Wall -Wextra -Werror.
- AddressSanitizer + UndefinedBehaviorSanitizer: PASS with detect_leaks=0.
  LeakSanitizer cannot run under the execution environment's tracing setup;
  transport state machine uses fixed storage and no heap allocation.
- MTU 23/185/247/517, 1/706/2048-byte boundary roundtrips: PASS.
- Notification enqueue failure leaves cursor unchanged: PASS.
- ACK mismatch/early ACK rejection and late matching ACK: PASS.
- ACK timeout retries, three-attempt stall with head retained: PASS.
- Disconnected queue/reconnection, oldest-first overflow, ID exhaustion: PASS.
- Python receiver tests: 5 passed (signed opaque bytes at three chunk sizes,
  partial retries/timeout, invalid/conflicting frames, durable duplicate,
  storage failure).
- Python syntax compilation: PASS.

Signed fixture is synthetic: 642 test bytes + 64-byte Ed25519 signature = 706.
It is not the user's original signed JSON envelope and proves no schema or
firmware serializer compatibility. The C tests separately check byte roundtrips;
Python tests do not exercise real BLE callbacks or Android Flutter behavior.

NOT executed: ESP-IDF target build, flashing, board radio test, authenticated BLE
pairing, Flutter integration, backend integration, power-loss recovery, concurrent
sensor load or RF performance measurements. No radio performance claims made.
