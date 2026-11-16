# ROVO Standalone Signed-Only Simulation — Test Status

Previous v1 branch: 28 local tests passed for the *old* encrypted/signature packet format. This revision switches to signed-only packet v2; previous counts are not valid evidence for v2.

After signed-only refactor, tests executed in the assistant's local isolated Python environment:

```
python -m pytest -q test_phase1.py test_phase2.py test_phase3.py
41 passed, 2 skipped
```

Two native C++/Monocypher interoperability tests were skipped here because Monocypher sources and the compiled native binary were unavailable in this environment. The user should run `./build.sh` followed by the full test suite on the macOS workstation, and record the new results before declaring C++ v2 interoperability passed. The official GitHub Actions workflow for the main project has not yet been checked for the new commit.

**Coverage includes:** Ed25519 verification, altered header/payload/signature, unknown device with the correct signing key, new report validation, 31-day-old valid retry after lost ACK, SQLite idempotency across restarts, ten offline reports, connection loss, and database initialization only at app startup.

This is a local functional/security regression suite, not a production security audit or full firmware/backend integration test.
