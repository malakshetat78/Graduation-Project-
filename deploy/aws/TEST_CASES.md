# T-004 test cases: backend and trust engine on AWS Lambda

Each test case: what is done (input), what must happen (expected), what happened (actual), and where the proof is.
Evidence files are in `results/`. Requirement IDs are from `verification/requirements.csv`. The full report is
`T004-REPORT.md`; the rows written to the shared database are listed in `results/test-records.md`.

Path tested: **client** -> **HTTPS (Lambda function URL)** -> **backend** (device, signature and freshness checks,
storage in the team Supabase database) -> **trust engine** (EventBridge schedule, once a minute).

Where: AWS = the live backend on AWS Lambda (`icvsp-backend`, us-east-1) with the team Supabase database,
HOST = laptop, LOCAL-PG = an isolated PostgreSQL in Docker on the laptop (`supabase/postgres:17.11.0.003`, the CI
image; never connected to Supabase), EMULATOR = the ECU firmware in the ESP32-S3 emulator (Shahesta).

Test script: `integration_test.py` (Part A read-only; Part B dry run unless `--execute`; the test device is revoked
in a `finally` block). All live runs on 16 Nov 2026 (UTC).

## Deployment and access (read-only)

| ID | Where | Input | Expected | Actual | Result | Requirements | Evidence |
|---|---|---|---|---|---|---|---|
| TC-T004-01 | AWS | `GET /health` | 200 | 200 `{"status":"ok"}`, 2.7 s (cold start) | PASS | (supporting) | `results/t004-partA-20261116T183530Z.json` |
| TC-T004-02 | AWS | `GET /ready` | 200, database reachable | 200 `ready`, 1.3 s | PASS | (supporting) | same |
| TC-T004-03 | AWS | `POST /devices` without the admin token | refused, nothing stored | 401 `admin token required` | PASS | FR-SEC-02, FR-SEC-04 | same, `results/cloudwatch-partA.txt` |
| TC-T004-04 | AWS | `GET /admin/overview` without the admin token | refused | 401 | PASS | FR-SEC-02 | same |
| TC-T004-05 | AWS | `GET /hazards` without sign-in | refused | 401 `sign-in required` | PASS | FR-SEC-01, FR-SEC-19 | same |
| TC-T004-06 | AWS | `GET /reports/{device}/{event}` without sign-in | refused | 401 | PASS | FR-SEC-01 | same |

## Signed reports on the live backend (approved minimal plan, one test device)

| ID | Where | Input | Expected | Actual | Result | Requirements | Evidence |
|---|---|---|---|---|---|---|---|
| TC-T004-07 | AWS | register `t004-test-01` with a fresh Ed25519 public key (admin token read from SSM, not printed) | 201 ACTIVE | 201, pairing code hidden | PASS | FR-SEC-03, FR-SEC-04 | `results/t004-partB-20261116T191409Z.json` |
| TC-T004-08 | AWS | valid signed pothole report | 201 accepted | 201 accepted, 1.2 s | PASS | FR-SEC-05, FR-SEC-07, FR-SEC-09 | same, `results/cloudwatch-partB.txt` |
| TC-T004-09 | AWS | the same report again | duplicate, not stored twice | 201 `duplicate` | PASS | FR-06, FR-SEC-10 | same |
| TC-T004-10 | AWS | report 3 days old | refused as stale | 422 `stale` | PASS | FR-SEC-10 | same |
| TC-T004-11 | AWS | report with an invalid signature | refused | 401 `bad_signature` | PASS | FR-SEC-07 | same |
| TC-T004-12 | AWS | signed report with one field changed afterwards (`severity`) | refused | 401 `bad_signature` | PASS | FR-SEC-07, NFR-10 | same |
| TC-T004-13 | AWS | report from a device that was never registered | refused | 403 `unknown_device` | PASS | FR-SEC-03, FR-SEC-07 | same |
| TC-T004-14 | AWS | wait for the 1-minute trust engine, read the operator view | report evaluated, hazard decision recorded | `evaluated`; hazard **suppressed** (one unvetted source, no driver warning) | PASS | FR-12, FR-14 | same |
| TC-T004-15 | AWS | revoke `t004-test-01` (runs even if a check fails), read its status | 200, status REVOKED | 200, REVOKED | PASS | FR-SEC-12 | same |
| TC-T004-16 | AWS | report in the revoked device's name | refused | 403 `device_revoked` | PASS | FR-SEC-12 | `results/t004-partB11-20261116T193030Z.json` |

## Database restart and failure (isolated, not the shared database)

| ID | Where | Input | Expected | Actual | Result | Requirements | Evidence |
|---|---|---|---|---|---|---|---|
| TC-T004-17 | LOCAL-PG | the team's backend test suite with `ICVSP_TEST_DATABASE_URL` set (includes server restart, database down then retry, two evaluators at once, RLS) | all pass, none skipped | 188/188, 0 skipped | PASS | as listed for the B- tests in `verification/traceability.csv` | `results/partC-postgres-tests.log` |

## ECU to the live backend (with Shahesta)

| ID | Where | Input | Expected | Actual | Result | Requirements | Evidence |
|---|---|---|---|---|---|---|---|
| TC-T004-18 | EMULATOR + AWS | register `ecu-qemu-cloud-01` with the ECU's public key; the ECU firmware sends a signed report over HTTPS | backend logs show it received and stored | CloudWatch `POST /reports 201` at 19:50:42.3 UTC (683 ms); event `ecu-qemu-cloud-01-1794858622-1`, detected 19:50:37, received 19:50:41 | PASS | FR-SEC-05, FR-SEC-07, FR-SEC-08 | `results/cloudwatch-ecu-qemu.txt`; ECU side: Shahesta's `testing/hardware/ecu/TEST_CASES.md` TC-15 |
| TC-T004-19 | AWS | that report after the 1-minute trust engine runs (19:50:56, 19:51:58, 19:52:56 UTC) | evaluated, hazard decision, no driver warning from one unvetted source | `evaluation: evaluated`; hazard **suppressed**, "No warning. Logged for review.", net evidence +0.14 | PASS | FR-12, FR-14, FR-15 | `results/ecu-qemu-overview.json`; Shahesta's TC-17 |

After these tests `ecu-qemu-cloud-01` was revoked (20:29:12 UTC, status read back: REVOKED).

## Not covered here

Load and concurrency on the live stack (excluded by agreement with the team), signed backend-to-phone alerts
(FR-SEC-15 to 17, not implemented), rate limiting (FR-SEC-20, branch `moaz/rate-limits` not merged). The trust
engine runs at most once a minute on Lambda (see `T004-REPORT.md`, known limitations).
