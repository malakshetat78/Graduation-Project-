# ICVSP backend

Receives the signed reports that phones forward from the in-vehicle devices, checks them, stores them, runs the trust
engine (`engine/`) in the background and answers "which hazards are ahead of me?". FastAPI on Supabase (PostgreSQL +
PostGIS + Auth); schema and access rules in `database/`. A SQLite store with the same methods (`store.py`) serves the
system simulation and quick local runs. What the app must do with each answer: `docs/backend-mobile-contract.md`.

```bash
cd backend
pip install -r requirements.txt
python3 -m unittest -v              # API tests on SQLite; set ICVSP_TEST_DATABASE_URL to run them on PostgreSQL too

# quick local run, no database server and no sign-in
ICVSP_ALLOW_ANONYMOUS=1 ICVSP_ADMIN_TOKEN=dev uvicorn --factory icvsp_backend.app:create_app --reload
# on Supabase: set the variables in .env.example
uvicorn --factory icvsp_backend.app:create_app
# interactive API docs: http://127.0.0.1:8000/docs
# operator dashboard:   http://127.0.0.1:8000/dashboard (asks for the admin token)
```

| Endpoint | Who | What it does |
|---|---|---|
| `POST /reports` | device signature | Body: the signed report line **exactly as the device sent it**. Checks, in order: size and format → device registered and active → signature with the *registered* key → schema v2 → envelope matches event → already stored? → time window → stored together with the device's counter state. A 201 means durably stored, not that the hazard is real |
| `GET /reports?device_id=`, `GET /reports/{device_id}/{event_id}` | owner, admin | Stored reports with their evaluation result and hazard |
| `POST /beacons` | owner, admin | 1 Hz positions from the phone (`vehicle_id` = device ID): needed for plausibility and "drove past and saw nothing". Kept 2 hours |
| `POST /hazards/ahead` | signed in | The driving view. Body: position, `heading_deg`, `speed_mps`, optional `route`. Hazards ahead on this road, the distance, and `alert: true` once per hazard on entering the alert range. The position is not stored |
| `GET /hazards?lat=&lon=&heading_deg=&radius_m=` | signed in | Hazards nearby: `advisory` (unconfirmed) or `warning` (corroborated / confirmed), with the engine's reasons |
| `GET /hazards/{id}` | signed in | One published hazard; administrators also get its evidence |
| `GET /me`, `POST /me/devices`, `GET /devices/{id}` | signed in, owner | The user's role and devices; links a device with its one-time pairing code |
| `POST /devices`, `POST /devices/{id}/revoke`, `POST /devices/{id}/pairing-code` | admin | Register a device and its Ed25519 public key (controlled provisioning, FR-SEC-03/04), returning the pairing code once; `vetted` gives a known fleet device a starting history. Revoke a device: its reports are refused from then on (FR-SEC-12) |
| `PUT /admin/users/{id}/role` | admin | Driver, ambulance or administrator |
| `GET /admin/overview`, `POST /admin/evaluate`, `POST /admin/settle` | admin | Operator view used by `/dashboard`; run the evaluator now |
| `GET /health`, `GET /ready` | anyone | Process alive; database reachable |

"admin" is `X-Admin-Token` (provisioning) or a signed-in administrator.

**The evaluator** (`worker.py`) runs in the API process every `ICVSP_WORKER_INTERVAL_S` seconds, or on its own with
`python -m icvsp_backend.worker`. One run is one database transaction: hazards, evidence, report results and reputation
are saved together or not at all, so a crash leaves reports `pending` and the next run finishes them with nothing
counted twice. An advisory lock keeps it to one evaluator at a time across servers. It also applies the retention rules.

Every refused report is logged with its code and reason (`rejections` table).

**Not done yet** (security V-cycle): signed backend → phone alerts (FR-SEC-15–17); rate limits (FR-SEC-20), so
`POST /reports` and the rejection log are open to flooding until the hosting platform or this code limits them;
road distance without a planned route needs map data. The evaluator recalculates all current evidence on every
run, which suits a prototype fleet, not a national one. TLS comes from the hosting platform (FR-SEC-08).
