# Location-only hazard lookup: backend-to-client contract

The database query belongs to FastAPI, **not** to Flutter or the terminal client.
FastAPI selects PostgreSQL/Supabase via the server-side `DATABASE_URL` environment
variable. Never embed that connection string in a mobile app, a CLI client, or Git.

## Case 1: GPS position only (nearby, not necessarily ahead)

`GET /hazards?lat=30.0444&lon=31.2357`

- Inputs: latitude and longitude.
- Output: a JSON **array** of nearby published hazards (possibly `[]`).
- This does not determine the driver's road or travel direction. Do not claim a
  returned hazard is in front of the car.

## Case 2: GPS position + movement direction (ahead)

`POST /hazards/ahead` with JSON:

```json
{
  "lat": 30.0444,
  "lon": 31.2357,
  "heading_deg": 90,
  "speed_mps": 12,
  "include_advisories": true
}
```

- Output: an object with `hazards` array, alert distance and route status.
- Heading is degrees clockwise from north.
- Speed is meters/second; 0 is accepted.
- Optional route coordinates improve road relevance. Heading alone uses a
  straight-ahead corridor, **not** guaranteed road/lane map matching.
- Do not attempt to infer heading from a **single** location fix. A later mobile
  app can use the OS-reported course or infer direction from successive valid
  GPS samples, then send it with the current position.

## Local Windows smoke test

From the repo's `backend` directory, with the FastAPI process already running:

```cmd
curl.exe http://127.0.0.1:8000/ready
python test_hazards_ahead.py --lat 30.0444 --lon 31.2357
python test_hazards_ahead.py --lat 30.0444 --lon 31.2357 --heading 90 --speed 12
```

With no CLI arguments, the script prompts for latitude and longitude; heading
is optional. It does not connect directly to the database.

For **local testing only**, FastAPI can use `ICVSP_ALLOW_ANONYMOUS=1`.
Before connecting to a live database for a read-only smoke test, set
`ICVSP_WORKER_INTERVAL_S=0` to disable background evaluations. A deployment
must use Supabase user authentication rather than anonymous mode. Successful
`/ready` confirms database connectivity; a running Uvicorn process alone does
not.

Keep this PR unmerged until the server has valid PostgreSQL credentials and
both terminal requests have been tested end to end.
