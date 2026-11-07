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

"admin" is `X-Admin-Token` (provisioning) or a signed-in administrator. A signed-in user who is not an administrator gets
`403`; `401` is only for missing or invalid credentials (the app signs the user out on a 401). Failed sign-ins and refusals
by role are logged on the `icvsp.security` logger (time, reason, endpoint, client address, never the token).
`ICVSP_ALLOW_ANONYMOUS=1` together with `SUPABASE_URL` stops the server at start: anonymous mode is for a machine without sign-in.

## Rate limits (FR-SEC-20)

A refused request gets `429` with `Retry-After` (seconds) and `{"code": "rate_limited", "retryable": true}`; nothing was
done with it, so the app waits and sends the same request again (`docs/backend-mobile-contract.md`). Counted per:

| Who | Counted by | Default (requests per seconds) |
|---|---|---|
| `POST /reports` (no token) | client IP | 600 / 60. Wide, because many phones can share one mobile-network IP |
| `GET /health` | client IP | 120 / 60 |
| `GET /ready` (queries the database) | client IP | 30 / 60 |
| `/dashboard`, `/docs`, `/redoc`, `/openapi.json` | client IP | 60 / 60 |
| Every call with a token | user ID, not IP | 300 / 60 |
| `POST /me/devices` (pairing-code guesses) | user ID | 10 / 60 |
| Wrong `X-Admin-Token` | client IP | 10 / 60; over it the token is not even compared |
| Invalid bearer token | client IP | 30 / 60; over it the `401` becomes `429`. A valid token is never refused for this |

IPv6 addresses are counted per /64, because one device owns a whole /64 and can change its address inside it at will.
The counters hold at most 50,000 keys; the least recently used one is forgotten (a constant-time step, and never the key being counted).
Nothing is counted by device ID before the signature is checked, so nobody can use up another device's limit by
sending reports in its name. A refusal is logged on the `icvsp.security` logger, once per rule and key in every window
(rule, limit, IP or user ID, wait; never a token).
The log has a budget too, so nobody can fill it by failing on purpose: failed sign-in lines are limited to 20 a minute per IP and 300
a minute over all IPs (`ICVSP_RATE_LOG_IP`, `ICVSP_RATE_LOG_ALL`); the lines left out are counted and the next line written
starts with `auth_failure_suppressed count=N`. Settings (`.env.example`):

* **Client IP.** Behind Railway or Render every request comes from the platform's proxy, so set
  `ICVSP_TRUST_PROXY_HEADERS=1`: the address is then taken from `X-Forwarded-For`, from the right
  (`ICVSP_TRUSTED_PROXY_HOPS`, default 1), because the proxy adds the address it saw there and everything further left
  is what the client wrote. **It is off by default.** Then the header is ignored, so nobody can pick their own "IP" by
  sending it, but behind a proxy all clients look like one. Turn it on only when the server cannot be reached without
  the proxy. With it on and no header (a laptop demo) the connection's own address is used, so the same code runs both ways.
* **Numbers.** `ICVSP_RATE_<NAME>=requests/seconds`, e.g. `ICVSP_RATE_REPORTS=300/60`; a wrong value stops the server at start.
* **Off.** `ICVSP_RATE_LIMIT_ENABLED=0`. In an `ICVSP_ALLOW_ANONYMOUS=1` development run (simulation, laptop demo) the
  limits are off unless `ICVSP_RATE_LIMIT_ENABLED=1`.
* **One process only.** The counters are in memory (`ratelimit.py`), per process. With more than one server process
  each counts alone, so the limits become several times larger: replace them with a shared counter (Redis, or a table)
  before scaling out.

**The evaluator** (`worker.py`) runs in the API process every `ICVSP_WORKER_INTERVAL_S` seconds, or on its own with
`python -m icvsp_backend.worker`. One run is one database transaction: hazards, evidence, report results and reputation
are saved together or not at all, so a crash leaves reports `pending` and the next run finishes them with nothing
counted twice. An advisory lock keeps it to one evaluator at a time across servers. It also applies the retention rules.

Every refused report is logged with its code and reason (`rejections` table).

**Not done yet** (security V-cycle): signed backend → phone alerts (FR-SEC-15–17); rate limits (FR-SEC-20), so
`POST /reports` and the rejection log are open to flooding until the hosting platform or this code limits them;
road distance without a planned route needs map data. The evaluator recalculates all current evidence on every
run, which suits a prototype fleet, not a national one. TLS comes from the hosting platform (FR-SEC-08).
