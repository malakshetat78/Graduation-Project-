# How to work in this repository

This is the team repository for ICVSP. It starts from the working prototype (device code, trust engine, backend,
system simulation, AI models and the V-cycle checks). `README.md` explains the project and its results.

## Where things are

| Folder | What it is | Who |
|---|---|---|
| `backend/` | Server (FastAPI): signed-report checks, device registry, hazards, reputation, dashboard. Endpoints in `backend/README.md` | Shahesta, Moaz |
| `engine/` | Trust and consensus engine; message format v2 in `engine/schema/safety_event.schema.json` | Shahesta |
| `sil/` | System simulation of the whole chain, without hardware | Shahesta |
| `verification/` | Requirements list, requirement → test traceability, CI check | Shahesta, security team |
| `firmware/` | Device code (ESP32-S3): IMU detector, camera decision logic, signed reports | Shahesta, Malak |
| `ai/` | Camera detectors, training notebooks, labelling policy (`ai/LABELLING_POLICY.md`) | Shahesta, Rana |
| `mobile/` (new) | Flutter app, BLE protocol, GPS features | Mariam |
| `database/` (new) | Supabase schema and migrations | Rana, Mariam |
| `cloud/` (new) | Deployment, hosting, monitoring | Ahmed, Malak |
| `docs/` (new) | Requirements, reports, design guides | everyone |

For the mobile app: the device sends the signed report in `verification/golden/device_reports_v2.jsonl`; the app
forwards it unchanged to `POST /reports`, sends positions to `POST /beacons` and asks `GET /hazards`.
For the database: the current tables are in `backend/icvsp_backend/store.py`.

## How to contribute

1. Never push to `main` directly. Make a branch per task: `name/task`, e.g. `mariam/ble-protocol`.
2. Open a pull request. GitHub Actions runs every test and the requirement traceability check on it.
3. Another team member reviews it. Merge only when the checks are green.
4. A new test must be listed in `verification/traceability.csv` with the requirement IDs it proves, or the check
   fails. Run the checks locally first (commands in `README.md`).

## Never commit

- Datasets, videos, recordings or frames (faces and number plates). They stay on your own machine.
- Models trained on private video frames.
- Passwords, tokens, API keys, private keys or `.env` files. Secrets come from environment variables.

`.gitignore` already keeps the known data and model folders out; check `git status` before every commit.
