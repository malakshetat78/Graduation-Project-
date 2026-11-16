# ROVO Standalone Signed-Only Simulation — Test Results

Packet v2 (Ed25519 signature only, no message encryption). Standalone demo: not the project's `signer.h` format
or `POST /reports` (see README.md and SECURITY_ARCHITECTURE.md).

## Run with the native C++ signer built (no skipped tests)

- Code under test: commit `ca2505f` (test files unchanged since)
- Date: 2026-11-16
- Machine: macOS 26.6.2 (arm64), Python 3.13.13, Apple clang version 21.0.0 (clang-2100.1.1.101)
- Monocypher 4.0.3, SHA-512 checked by `build.sh`

```bash
./build.sh
python -m pytest -v -rs test_phase1.py test_phase2.py test_phase3.py
```

Result: **43 passed, 0 skipped, 0 failed.** The two C++ tests ran (`test_optional_actual_cpp_binary`,
`test_phase3_real_cpp_packet_to_python_when_built`): packets signed by the C++/Monocypher program were verified by
Python `cryptography` and accepted (201).

An earlier run without the C++ build gave 41 passed and 2 skipped; those two skipped tests are the C++ to Python
check, so that run did not show C++/Python compatibility.

## In CI

GitHub Actions (`.github/workflows/verification.yml`) builds the C++ signer and runs the three files on every push;
`crypto.log` is kept with the run's evidence. Each test function is listed in `verification/traceability.csv`
(rows C-01 to C-23). The traceability check counts a skipped test as failed, so CI fails if the C++ tests do not run.

## What this does and does not show

Shows, for this demo format: C++ signs and Python verifies; any changed header, payload or signature byte is
rejected; a wrong key or an unregistered device ID is rejected; an identical retry is acknowledged once (also after
the 30-day window, for a lost ACK); a different packet reusing a message ID gets 409; offline queueing and retry.

Does not show: the project's real report format, an ESP32, BLE, TLS, or the real backend.
