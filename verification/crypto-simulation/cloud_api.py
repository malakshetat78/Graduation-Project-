"""Standalone ROVO signed report FastAPI/SQLite simulator.

Demo only: no real device provisioning, TLS termination, or integration with
ICVSP signer.h / production POST /reports endpoint.
"""
from __future__ import annotations

from contextlib import asynccontextmanager
from datetime import datetime, timedelta, timezone
import hashlib
import os
from pathlib import Path
import sqlite3

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse

from verify_packet import PACKET_LEN, PacketRejected, verify_signed_packet

MAX_OFFLINE_AGE = timedelta(days=30)
MAX_CLOCK_SKEW = timedelta(minutes=5)


def init_database(db_path: Path) -> None:
    db_path.parent.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(db_path, timeout=10) as db:
        db.execute("""CREATE TABLE IF NOT EXISTS reports (
            device_id TEXT NOT NULL,
            message_id TEXT NOT NULL,
            packet_hash TEXT NOT NULL,
            detected_at TEXT NOT NULL,
            received_at TEXT NOT NULL,
            latitude REAL NOT NULL,
            longitude REAL NOT NULL,
            hazard_type TEXT NOT NULL,
            PRIMARY KEY (device_id, message_id)
        )""")


def check_existing(db_path: Path, decoded: dict, packet_hash: str) -> str | None:
    """Recognize an authenticated retry *before* applying the clock policy.

    Existing exact packet is idempotent even after 30 days; a different signed
    packet claiming the same (device_id, message_id) is a conflict.
    """
    with sqlite3.connect(db_path, timeout=10) as db:
        existing = db.execute(
            'SELECT packet_hash FROM reports WHERE device_id = ? AND message_id = ?',
            (decoded['device_id'], decoded['message_id']),
        ).fetchone()
    if existing is None:
        return None
    if existing[0] == packet_hash:
        return 'duplicate'
    raise HTTPException(status_code=409, detail='Conflicting message ID')


def record_report(db_path: Path, decoded: dict, packet_hash: str) -> str:
    """Atomic insert-or-duplicate with primary key protection for concurrent writes."""
    with sqlite3.connect(db_path, timeout=10) as db:
        cursor = db.execute(
            """INSERT OR IGNORE INTO reports
            (device_id, message_id, packet_hash, detected_at, received_at,
             latitude, longitude, hazard_type)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?)""",
            (
                decoded['device_id'], decoded['message_id'], packet_hash,
                decoded['timestamp_utc'], datetime.now(timezone.utc).isoformat(),
                decoded['latitude'], decoded['longitude'], decoded['hazard_type'],
            ),
        )
        if cursor.rowcount == 1:
            return 'accepted'
    # An overlapping request may have inserted the same ID during the write.
    return check_existing(db_path, decoded, packet_hash) or 'duplicate'


def create_app(*, db_path: str | Path | None = None, keys_dir: str | Path | None = None) -> FastAPI:
    database = Path(db_path if db_path is not None else os.getenv('ROVO_CLOUD_DB', 'data/cloud_reports.sqlite3'))
    demo_keys = Path(keys_dir if keys_dir is not None else os.getenv('ROVO_KEYS_DIR', 'keys'))

    @asynccontextmanager
    async def lifespan(_app: FastAPI):
        init_database(database)
        yield

    app = FastAPI(title='ROVO Signed-Only Simulation', version='0.3.0', lifespan=lifespan)

    @app.get('/healthz')
    def healthz():
        return {'status': 'ok', 'service': 'rovo-signed-simulation'}

    @app.post('/v1/hazards')
    async def ingest_hazard(request: Request):
        media_type = request.headers.get('content-type', '').split(';', 1)[0].lower().strip()
        if media_type != 'application/octet-stream':
            raise HTTPException(status_code=415, detail='Use application/octet-stream')
        packet = bytearray()
        async for chunk in request.stream():
            if len(packet) + len(chunk) > PACKET_LEN:
                raise HTTPException(status_code=413, detail='Packet too large')
            packet.extend(chunk)
        if len(packet) != PACKET_LEN:
            raise HTTPException(status_code=422, detail='Incorrect packet size')
        raw = bytes(packet)
        try:
            decoded = verify_signed_packet(raw, demo_keys)
            detected_at = datetime.fromisoformat(decoded['timestamp_utc'])
        except (PacketRejected, OSError, ValueError, OverflowError) as exc:
            raise HTTPException(status_code=422, detail='Invalid signed packet') from exc

        packet_hash = hashlib.sha256(raw).hexdigest()
        try:
            outcome = check_existing(database, decoded, packet_hash)
        except sqlite3.Error as exc:
            raise HTTPException(status_code=503, detail='Database unavailable') from exc
        if outcome is None:
            now = datetime.now(timezone.utc)
            if detected_at > now + MAX_CLOCK_SKEW:
                raise HTTPException(status_code=422, detail='Timestamp is in the future')
            if detected_at < now - MAX_OFFLINE_AGE:
                raise HTTPException(status_code=422, detail='Timestamp exceeds offline acceptance window')
            try:
                outcome = record_report(database, decoded, packet_hash)
            except sqlite3.Error as exc:
                raise HTTPException(status_code=503, detail='Database unavailable') from exc
        return JSONResponse(
            status_code=200 if outcome == 'duplicate' else 201,
            content={
                'status': outcome,
                'device_id': decoded['device_id'],
                'message_id': decoded['message_id'],
                'hazard_type': decoded['hazard_type'],
                'packet_bytes': len(raw),
            },
        )

    return app


# Construct routes on import; database initialization happens ONLY in lifespan.
app = create_app()
