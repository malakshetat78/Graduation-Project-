"""HTTP API (FastAPI). Run locally:  cd backend && ICVSP_ALLOW_ANONYMOUS=1 uvicorn --factory icvsp_backend.app:create_app --reload
Interactive API docs at http://127.0.0.1:8000/docs. Configuration: backend/.env.example.

Reports are authenticated by the device's signature, whichever phone relays them. Everything about a person (their
position, their devices, the hazards shown to them) needs a Supabase Auth token. The database connection passes RLS,
so the checks in this file are what keeps one user away from another user's data. TLS comes from the hosting
platform (FR-SEC-08)."""
from __future__ import annotations

import hmac
import logging
import os
import time
from contextlib import asynccontextmanager
from pathlib import Path
from typing import Callable, Literal
from uuid import UUID

from fastapi import Depends, FastAPI, Header, HTTPException, Query, Request
from fastapi.concurrency import run_in_threadpool
from fastapi.responses import HTMLResponse, JSONResponse
from pydantic import BaseModel, Field, model_validator

from .ahead import AheadConfig
from .auth import AuthUnavailable, InvalidToken, SupabaseVerifier, User
from .service import Service, hash_code
from .store import Store
from .store_errors import StoreUnavailable
from .verify import MAX_LINE, Rejected
from .worker import Worker

DASHBOARD = Path(__file__).with_name("dashboard.html")
RETRY_AFTER_S = 30
MAX_BEACONS = 3600
log = logging.getLogger("icvsp.api")

Latitude = Field(ge=-90, le=90, allow_inf_nan=False)
Longitude = Field(ge=-180, le=180, allow_inf_nan=False)


class DeviceIn(BaseModel):
    device_id: str = Field(min_length=1, max_length=64, pattern=r"^[^\x00]+$")
    public_key: str
    vetted: bool = False


class BeaconIn(BaseModel):
    vehicle_id: str = Field(min_length=1, max_length=64, pattern=r"^[^\x00]+$")
    t: float = Field(ge=0, le=32503680000, allow_inf_nan=False)   # Unix seconds
    lat: float = Latitude
    lon: float = Longitude
    speed_mps: float = Field(ge=0, allow_inf_nan=False)
    heading_deg: float = Field(allow_inf_nan=False)
    detector_on: bool = True


class ClaimIn(BaseModel):
    device_id: str = Field(min_length=1, max_length=64, pattern=r"^[^\x00]+$")
    pairing_code: str = Field(min_length=1, max_length=128)


class RoleIn(BaseModel):
    role: Literal["driver", "ambulance", "admin"]


class RoutePoint(BaseModel):
    lat: float = Latitude
    lon: float = Longitude


class AheadIn(BaseModel):
    lat: float = Latitude
    lon: float = Longitude
    heading_deg: float | None = Field(default=None, ge=0, lt=360, allow_inf_nan=False)
    speed_mps: float = Field(default=0.0, ge=0, allow_inf_nan=False)
    route: list[RoutePoint] | None = Field(default=None, min_length=2, max_length=2000)
    include_advisories: bool = False

    @model_validator(mode="after")
    def needs_a_direction(self):
        if self.heading_deg is None and self.route is None:
            raise ValueError("send heading_deg, or a route")
        return self


def store_from_env():
    url = os.environ.get("DATABASE_URL")
    if url:
        from .pg_store import PgStore   # only needs psycopg when it is actually used
        return PgStore(url)
    return Store(os.environ.get("ICVSP_DB", "icvsp.sqlite3"))


async def read_limited(request: Request, limit: int) -> bytes:
    """Stops reading at the limit, so a huge body costs nothing; the one extra byte makes the size check refuse it."""
    body = bytearray()
    async for chunk in request.stream():
        body += chunk
        if len(body) > limit:
            return bytes(body[:limit + 1])
    return bytes(body)


def create_app(store=None, clock: Callable[[], float] = time.time, admin_token: str | None = None,
               verifier=None, allow_anonymous: bool | None = None, worker_interval: float | None = None,
               ahead: AheadConfig | None = None) -> FastAPI:
    """`allow_anonymous` skips user sign-in, for local runs and the simulation only. Without it and without a
    configured verifier the server refuses to start, rather than running open by accident."""
    if allow_anonymous is None:
        allow_anonymous = os.environ.get("ICVSP_ALLOW_ANONYMOUS") == "1"
    if verifier is None and os.environ.get("SUPABASE_URL"):
        verifier = SupabaseVerifier(os.environ["SUPABASE_URL"], os.environ.get("SUPABASE_JWT_SECRET"))
    if verifier is None and not allow_anonymous:
        raise RuntimeError("set SUPABASE_URL for user sign-in (or ICVSP_ALLOW_ANONYMOUS=1 on a development machine)")
    if worker_interval is None:
        worker_interval = float(os.environ.get("ICVSP_WORKER_INTERVAL_S", "10")) if store is None else 0.0
    svc = Service(store or store_from_env(), ahead=ahead)
    token = admin_token if admin_token is not None else os.environ.get("ICVSP_ADMIN_TOKEN", "")

    @asynccontextmanager
    async def lifespan(_: FastAPI):
        worker = Worker(svc, worker_interval, clock) if worker_interval > 0 else None
        if worker:
            worker.start()
        yield
        if worker:
            worker.stop()

    app = FastAPI(title="ICVSP backend", version="0.2", lifespan=lifespan)
    app.state.service = svc

    @app.exception_handler(Rejected)
    def rejected(_: Request, r: Rejected):
        """A final answer: the phone stops retrying this report."""
        return JSONResponse({"detail": r.reason, "code": r.code, "retryable": False}, r.status)

    @app.exception_handler(StoreUnavailable)
    def unavailable(_: Request, e: StoreUnavailable):
        """Nothing was stored: the phone keeps the report and sends the same line again later."""
        log.warning("database unavailable: %s", e)
        return JSONResponse({"detail": "storage unavailable, retry later", "code": "storage_unavailable",
                             "retryable": True}, 503, headers={"Retry-After": str(RETRY_AFTER_S)})

    def current_user(authorization: str | None = Header(default=None)) -> User | None:
        """None only in anonymous (development) mode with no token sent."""
        if authorization is None and allow_anonymous:
            return None
        scheme, _, credentials = (authorization or "").partition(" ")
        if scheme.lower() != "bearer" or not credentials or verifier is None:
            raise HTTPException(401, "sign-in required", headers={"WWW-Authenticate": "Bearer"})
        try:
            user_id = verifier.verify(credentials.strip())
        except InvalidToken:
            raise HTTPException(401, "invalid or expired token", headers={"WWW-Authenticate": "Bearer"}) from None
        except AuthUnavailable:
            raise HTTPException(503, "sign-in service unavailable, retry later",
                                headers={"Retry-After": str(RETRY_AFTER_S)}) from None
        profile = svc.store.profile(user_id)
        if profile is None:
            raise HTTPException(403, "no profile for this account")
        return User(user_id, profile["role"])

    def require_signed_in(user: User | None = Depends(current_user)) -> User:
        if user is None:
            raise HTTPException(401, "sign-in required")
        return user

    def require_admin(x_admin_token: str | None = Header(default=None),
                      authorization: str | None = Header(default=None)) -> None:
        """The provisioning token, or a signed-in administrator (FR-SEC-02)."""
        if token and hmac.compare_digest((x_admin_token or "").encode(), token.encode()):
            return
        if authorization is not None and current_user(authorization).role == "admin":
            return
        raise HTTPException(401, "admin token required")

    def check_device_access(user: User | None, device_id: str) -> None:
        """Same answer for someone else's device and for one that does not exist."""
        if user is not None and user.role != "admin" and not svc.store.owns_device(user.id, device_id):
            raise HTTPException(403, "not your device")

    @app.get("/health")
    def health():
        return {"status": "ok"}

    @app.get("/ready")
    def ready():
        svc.store.ping()
        return {"status": "ready", "reports_pending": svc.store.pending_count()}

    # ---------- devices ----------
    @app.post("/devices", status_code=201, dependencies=[Depends(require_admin)])
    def register(d: DeviceIn):
        code = svc.register(d.device_id, d.public_key, clock(), d.vetted)
        return {"device_id": d.device_id, "status": "ACTIVE", "pairing_code": code}

    @app.post("/devices/{device_id}/revoke", dependencies=[Depends(require_admin)])
    def revoke(device_id: str):
        if not svc.store.set_status(device_id, "REVOKED"):
            raise HTTPException(404, "unknown device")
        return {"device_id": device_id, "status": "REVOKED"}

    @app.post("/devices/{device_id}/pairing-code", dependencies=[Depends(require_admin)])
    def pairing_code(device_id: str):
        """A new one-time code, e.g. for a second driver of the same car."""
        code = svc.new_pairing_code(device_id)
        if code is None:
            raise HTTPException(404, "unknown device")
        return {"device_id": device_id, "pairing_code": code}

    @app.get("/devices/{device_id}")
    def device(device_id: str, user: User | None = Depends(current_user)):
        check_device_access(user, device_id)
        d = svc.store.device(device_id)
        if d is None:
            raise HTTPException(404, "unknown device")
        return {"device_id": d["device_id"], "status": d["status"], "good": d["good"], "bad": d["bad"],
                "counters": _counters(svc.store.counters(device_id))}

    def _counters(c) -> dict | None:
        return None if c is None else {"highest_boot_id": c["highest_boot_id"], "highest_sequence": c["highest_sequence"],
                                       "reports_stored": c["reports_stored"]}

    # ---------- the signed-in user ----------
    @app.get("/me")
    def me(user: User = Depends(require_signed_in)):
        return {"user_id": user.id, "role": user.role, "devices": svc.store.user_devices(user.id)}

    @app.post("/me/devices", status_code=201)
    def claim(c: ClaimIn, user: User = Depends(require_signed_in)):
        """Links a device to this account with the one-time code from provisioning."""
        if not svc.store.claim_device(user.id, c.device_id, hash_code(c.pairing_code), clock()):
            raise HTTPException(403, "wrong device or pairing code")
        return {"device_id": c.device_id}

    @app.put("/admin/users/{user_id}/role", dependencies=[Depends(require_admin)])
    def set_role(user_id: UUID, body: RoleIn):
        if not svc.store.set_role(str(user_id), body.role):
            raise HTTPException(404, "unknown user")
        return {"user_id": str(user_id), "role": body.role}

    # ---------- reports ----------
    @app.post("/reports", status_code=201)
    async def report(request: Request):
        """Body: the signed report line exactly as the device sent it over BLE. A 201 means durably stored, so the
        phone may delete its copy; it says nothing about the hazard being real ("evaluation" shows how far that is)."""
        raw = await read_limited(request, MAX_LINE)
        return await run_in_threadpool(svc.submit, raw, clock())

    @app.get("/reports")
    def list_reports(device_id: str, limit: int = Query(default=50, ge=1, le=200),
                     user: User | None = Depends(current_user)):
        check_device_access(user, device_id)
        return [svc.report_view(r) for r in svc.store.device_reports(device_id, limit)]

    @app.get("/reports/{device_id}/{event_id}")
    def read_report(device_id: str, event_id: str, user: User | None = Depends(current_user)):
        check_device_access(user, device_id)
        r = svc.store.report(device_id, event_id)
        if r is None:
            raise HTTPException(404, "unknown report")
        return svc.report_view(r)

    @app.post("/beacons")
    def beacons(items: list[BeaconIn] = Depends(_limit_beacons), user: User | None = Depends(current_user)):
        """Positions of the user's own devices only: another vehicle's fake trace would be evidence against a hazard."""
        if user is not None and user.role != "admin":
            ids = list({b.vehicle_id for b in items})
            if svc.store.owned_among(user.id, ids) != set(ids):
                raise HTTPException(403, "not your device")
        return {"stored": svc.add_beacons([b.model_dump() for b in items])}

    # ---------- hazards ----------
    @app.get("/hazards")
    def hazards(lat: float = Query(ge=-90, le=90), lon: float = Query(ge=-180, le=180),
                radius_m: float = Query(default=1000.0, gt=0, le=50000),
                heading_deg: float | None = Query(default=None, ge=0, lt=360),
                _: User | None = Depends(current_user)):
        return svc.hazards(clock(), lat, lon, radius_m, heading_deg)

    @app.post("/hazards/ahead")
    def hazards_ahead(body: AheadIn, user: User | None = Depends(current_user)):
        """The driving view: hazards ahead on this user's road, with the distance and whether to alert now. A POST so
        the position does not end up in URL logs; it is not stored."""
        route = [(p.lat, p.lon) for p in body.route] if body.route else None
        return svc.hazards_ahead(clock(), user.id if user else None, body.lat, body.lon, body.heading_deg,
                                 body.speed_mps, route, body.include_advisories)

    @app.get("/hazards/{hazard_id}")
    def hazard(hazard_id: UUID, user: User | None = Depends(current_user)):
        """Drivers see published, unexpired hazards; administrators also see the evidence behind any hazard."""
        h = svc.store.hazard(str(hazard_id))
        privileged = user is None or user.role == "admin"
        if h is None or not (privileged or (h["state"] in ("corroborated", "confirmed") and h["expires_at"] > clock())):
            raise HTTPException(404, "unknown hazard")
        out = svc._view(h, 0.0)
        del out["distance_m"]
        if privileged:
            out["evidence"] = svc.store.hazard_evidence(str(hazard_id))
        return out

    # ---------- operators ----------
    @app.get("/admin/overview", dependencies=[Depends(require_admin)])
    def overview(recent: int = 30):
        """Operator view used by the dashboard (/dashboard)."""
        return svc.overview(clock(), max(1, min(recent, 200)))

    @app.get("/dashboard", response_class=HTMLResponse, include_in_schema=False)
    def dashboard():
        return DASHBOARD.read_text(encoding="utf-8")

    @app.post("/admin/settle", dependencies=[Depends(require_admin)])
    def settle():
        return {"updates": svc.settle(clock())}

    @app.post("/admin/evaluate", dependencies=[Depends(require_admin)])
    def evaluate():
        """Runs the evaluator now, without waiting for the worker."""
        return svc.evaluate(clock(), settle=True)

    return app


def _limit_beacons(items: list[BeaconIn]) -> list[BeaconIn]:
    if len(items) > MAX_BEACONS:
        raise HTTPException(413, f"at most {MAX_BEACONS} positions per request")
    return items
