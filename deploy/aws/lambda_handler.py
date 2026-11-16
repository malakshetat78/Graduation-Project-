"""AWS Lambda entry point for the ICVSP backend (backend/icvsp_backend, unchanged).

Two kinds of call arrive here:
- HTTPS requests through the function URL (the ECU's POST /reports, phones, the dashboard): passed to the
  FastAPI app by Mangum, which turns the Lambda event into a normal web request and the answer back.
- {"icvsp_task": "evaluate"} from EventBridge Scheduler once a minute: runs the trust engine on pending
  reports and the retention rules (what the background worker thread does on an always-on server).

Secrets are read from SSM Parameter Store (SecureString) at cold start, never from the template or the code.
Reports are only stored in PostgreSQL (Supabase): Lambda's disk is temporary, so a report acknowledged as
stored must never sit in a local SQLite file. Without DATABASE_URL the function refuses to start.
"""
from __future__ import annotations

import logging
import os
import time

logging.getLogger().setLevel(logging.INFO)
log = logging.getLogger("icvsp.lambda")


def _load_secrets() -> None:
    names = {k: os.environ[k] for k in ("ICVSP_SSM_DATABASE_URL", "ICVSP_SSM_ADMIN_TOKEN") if os.environ.get(k)}
    if not names:
        return
    import boto3  # included in the Lambda Python runtime

    got = boto3.client("ssm").get_parameters(Names=list(names.values()), WithDecryption=True)
    values = {p["Name"]: p["Value"] for p in got["Parameters"]}
    missing = [n for n in names.values() if n not in values]
    if missing:
        raise RuntimeError(f"SSM parameters missing: {', '.join(missing)} (see deploy/aws/README.md, step 2)")
    target = {"ICVSP_SSM_DATABASE_URL": "DATABASE_URL", "ICVSP_SSM_ADMIN_TOKEN": "ICVSP_ADMIN_TOKEN"}
    for env_key, param in names.items():
        os.environ[target[env_key]] = values[param]


def _build():
    _load_secrets()
    from icvsp_backend.app import create_app
    from icvsp_backend.worker import Worker

    url = os.environ.get("DATABASE_URL")
    if url:
        from icvsp_backend.pg_store import PgStore
        # One request at a time per Lambda instance, so two connections are enough. The total across instances
        # is capped by the function's reserved concurrency (template.yaml), not by this number.
        store = PgStore(url, max_connections=2)
    elif os.environ.get("ICVSP_LOCAL_TEST") == "1":  # local container test only; never set in template.yaml
        from icvsp_backend.store import Store
        store = Store(os.environ.get("ICVSP_DB", "/tmp/icvsp.sqlite3"))
    else:
        raise RuntimeError("DATABASE_URL is not set: refusing to store reports on Lambda's temporary disk")
    app = create_app(store=store)  # with a store given, no background thread: the schedule runs the evaluator
    return app, Worker(app.state.service, 0)


_app, _worker = _build()

from mangum import Mangum  # noqa: E402

_asgi = Mangum(_app, lifespan="off")
_last_call = time.monotonic()
_IDLE_CHECK_S = 30.0


def _check_idle_connections() -> None:
    """A frozen Lambda instance can find its pooled database connections closed by Supabase. After an idle gap,
    test them first, so the request gets a working connection instead of a 503."""
    global _last_call
    now = time.monotonic()
    pool = getattr(_app.state.service.store, "pool", None)
    if pool is not None and now - _last_call > _IDLE_CHECK_S:
        try:
            pool.check()  # drops broken idle connections; the pool opens new ones as needed
        except Exception:
            log.exception("connection check failed")
    _last_call = now


def handler(event, context):
    _check_idle_connections()
    if isinstance(event, dict) and event.get("icvsp_task") == "evaluate":
        _worker.run_once()  # logs and keeps reports pending on any error; the next minute retries
        return {"ok": True}
    return _asgi(event, context)
