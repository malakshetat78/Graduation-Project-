# How to work in this repository

This is the team repository for ICVSP. It starts from the working prototype (device code, trust engine, backend,
system simulation, AI models and the V-cycle checks). `README.md` explains the project and its results.

## Where things are

| Folder | What it is | Who |
|---|---|---|
| `3_WEB_SW/code/backend_sw/` | Server (FastAPI): signed-report checks, device registry, hazards, reputation, dashboard. Endpoints in its `README.md` | Shahesta, Moaz |
| `3_WEB_SW/code/engine/` | Trust and consensus engine; message format v2 in `schema/safety_event.schema.json` | Shahesta |
| `3_WEB_SW/code/database/` | Supabase schema and migrations | Rana, Mariam |
| `3_WEB_SW/code/deploy/` | AWS deployment | Nourseen, Ahmed, Malak |
| `3_WEB_SW/doc/` | Hand-off notes, backend-mobile contract, AWS hosting research | everyone |
| `5_TESTING/` | Board test cases and logs (`doc`), traceability check and laptop tests (`code`) | everyone with a hardware task, security team |
| `1_XAIO_SW/` | Device firmware (ESP-IDF, ESP32-S3): camera, IMU, GNSS, BLE, detectors, signed reports | Shahesta, Malak, Rana, Ahmed, Mariam |
| `4_AI_SW/` | Camera detectors, training notebooks; labelling policy in `4_AI_SW/doc/LABELLING_POLICY.md` | Shahesta, Rana |
| `2_FLUTTER_SW/` | Flutter app, BLE protocol, GPS features | Mariam |
| `6_TOOLS/` | Shared scripts and utilities | everyone |

For the mobile app: the device sends the signed report in `5_TESTING/code/verification/golden/device_reports_v2.jsonl`; the app
forwards it unchanged to `POST /reports`, sends positions to `POST /beacons` and asks `POST /hazards/ahead`. What each
answer means for the phone's queue is in `3_WEB_SW/doc/backend-mobile-contract.md`.
For the database: the migrations are in `3_WEB_SW/code/database/supabase/migrations`, see `3_WEB_SW/code/database/README.md`.

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
