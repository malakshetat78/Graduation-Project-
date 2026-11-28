# 3_WEB_SW / code

| Folder | What |
|---|---|
| [`engine/`](engine/) | trust & consensus engine (plain Python, 52 tests) |
| [`backend_sw/`](backend_sw/) | server (FastAPI): signed-report checks, storage, evaluator, dashboard |
| [`database/`](database/) | Supabase migrations and config |
| [`deploy/`](deploy/) | AWS Lambda deployment |

Run the tests: `cd engine && python3 -m unittest -v`, `cd backend_sw && python3 -m unittest -v`.
