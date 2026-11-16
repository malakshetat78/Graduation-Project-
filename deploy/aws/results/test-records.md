# T-004: test records on the shared Supabase database

Approved by Shahesta (minimal controlled plan). Written by `deploy/aws/integration_test.py --part B --execute`
against the `icvsp-backend` Lambda stack. Part A and Part C write nothing to Supabase.

**Status: PLANNED, not run yet.** This file is updated with the actual run time and IDs after Part B.

## Identities

| ID | Role |
|---|---|
| `t004-test-01` | the only registered test device; key generated in memory for the run, never saved, not an ECU key |
| `t004-test-unregistered` | never registered; only proves unknown devices are refused |

Location of every test report: **lat 23.5, lon 27.0** (Western Desert), far from the team's routes.

## Planned writes

| Check | Request | Expected | Database effect |
|---|---|---|---|
| B1 | `POST /devices` (admin token) | 201 | 1 row in `devices` (`t004-test-01`, not vetted) |
| B2 | `POST /reports` valid signed report | 201 accepted | 1 row in `reports` (+ the device's counter in `device_counters`) |
| B3 | same report again | 201 duplicate | none |
| B4 | report 3 days old | 422 stale | 1 row in `rejections` |
| B5 | invalid signature | 401 | 1 row in `rejections` |
| B6 | payload changed after signing | 401 | 1 row in `rejections` |
| B7 | unregistered device | 403 | 1 row in `rejections` |
| B8 | 1-minute evaluator (read-only polling of `/admin/overview`) | evaluated | the evaluator may add 1 hazard + 1 evidence row for the test report |
| B9 | `POST /devices/t004-test-01/revoke` | 200 | `t004-test-01` status → `REVOKED` (runs even if a check fails) |

About 7 report calls and 2 admin calls in total. No load test, no concurrency test, no beacons, no schema change,
no existing row read into the evidence, changed or deleted.

## What stays afterwards

| Record | Count | Removed by |
|---|---|---|
| `devices`: `t004-test-01` (REVOKED) | 1 | nothing: the backend role cannot delete devices (by design). An administrator can remove it in Supabase |
| `reports` + `device_counters` | 1 + 1 | backend retention (reports 15 days) |
| `rejections` | 4 | backend retention (90 days) |
| `hazards` + `hazard_evidence` (if created) | 1 + 1 | not deleted; the hazard expires (pothole freshness about 48 h) |

## Why no driver can be alerted

Checked in the code, not assumed:

1. `service.hazards_ahead` sets `alert` only when the hazard's level is **warning** (`corroborated` or `confirmed`) and
   the driver is inside the alert range. A single new, unvetted device cannot corroborate its own report, so its
   hazard is at most `unconfirmed` (advisory, `alert: false`).
2. In the local rehearsal of this exact run, the evaluator put the test hazard in state **`suppressed`**, which is not
   shown to drivers at all.
3. The location is 23.5, 27.0, hundreds of kilometres from the routes the team drives.
4. The backend sends no push notifications; drivers only see hazards near their own position when they ask.
