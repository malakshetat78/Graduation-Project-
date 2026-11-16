# T-004: test records on the shared Supabase database

Approved by Shahesta (minimal controlled plan) and by Nourseen. Written by
`deploy/aws/integration_test.py --part B --execute` against the `icvsp-backend` Lambda stack (us-east-1).
Part A and Part C wrote nothing to Supabase.

## Runs

| Run (UTC) | Result | Written |
|---|---|---|
| 2026-11-16 19:05:30 | stopped at B1: 401, because the admin token stored in SSM ended in a newline (Windows bug, fixed in `set_secrets.sh`) | **nothing** (refused before registration; no report sent) |
| 2026-11-16 19:13:52 – 19:14:09 | **10/10 passed**, evidence `t004-partB-20261116T191409Z.json`, logs `cloudwatch-partB.txt` | the rows below |
| 2026-11-16 19:30:30 | **B11 passed** (added after review): a report in the revoked device's name → 403 `device_revoked`, evidence `t004-partB11-20261116T193030Z.json` | 1 row in `rejections` |

## Identities and location

| ID | Role |
|---|---|
| `t004-test-01` | the only registered test device; Ed25519 key generated in memory for the run, never saved, not an ECU key. Public key in the evidence file |
| `t004-test-unregistered` | never registered; no device row exists |

Every test report: **lat 23.5, lon 27.0** (Western Desert).

## Rows written (actual)

| Check | Answer | Database effect |
|---|---|---|
| B1 register `t004-test-01` | 201 | 1 row in `devices` (not vetted) |
| B2 valid signed report `t004-test-01-1-1` | 201 accepted | 1 row in `reports`, 1 row in `device_counters` |
| B3 same report again | 201 duplicate | none |
| B4 3-day-old report | 422 `stale` | 1 row in `rejections` |
| B5 invalid signature | 401 `bad_signature` | 1 row in `rejections` |
| B6 payload changed after signing | 401 `bad_signature` | 1 row in `rejections` |
| B7 unregistered device | 403 `unknown_device` | 1 row in `rejections` |
| B8 evaluator | report evaluated; test hazard state **`suppressed`** | evaluator's own rows for the test report (hazard decision, evidence, evaluation run) |
| B9 revoke | 200 | `t004-test-01` status → **REVOKED** (verified, B10) |
| B11 report as `t004-test-01` after revoke (new in-memory key, boot 2) | 403 `device_revoked` | 1 row in `rejections` |

Also changed during T-004 (not database): SSM `/icvsp/admin_token` rotated to version 2 (the first value was unusable),
and the Lambda description updated once to reload it. No other AWS resource was changed.

## What stays and when it goes

| Record | Count | Removed by |
|---|---|---|
| `devices`: `t004-test-01` (REVOKED) | 1 | nothing automatic; the backend role cannot delete devices. An administrator may remove it in Supabase |
| `reports` + `device_counters` | 1 + 1 | backend retention (reports: 15 days) |
| `rejections` | 5 (B4–B7, B11) | backend retention (90 days) |
| hazard decision for the test report | 1 | stays `suppressed`, then expires |

No existing row was changed or deleted. No schema change, no migration.

## Why no driver was or can be alerted

1. `service.hazards_ahead` raises `alert` only for level **warning** (`corroborated` / `confirmed`) inside the alert range.
2. The live run put the test hazard in state **`suppressed`**: not returned to drivers at all.
3. The test device is now REVOKED, and a report in its name is refused with 403 `device_revoked` (B11, tested live;
   the backend checks the device status before the signature, so this holds for any key).
4. 23.5, 27.0 is far from the team's routes, and the backend sends no push notifications.
