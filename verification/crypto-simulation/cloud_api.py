"""Phase 2 local Cloud Run simulator: FastAPI, signature check, AEAD decrypt,
validation, and SQLite idempotency/replay protection.

Educational local simulation only. No authentication for the forwarding mobile app,
rate limits, real key registry, encrypted key management, or HTTPS termination.
"""
from __future__ import annotations

from datetime import datetime, timedelta, timezone
import hashlib
import os
from pathlib import Path
import sqlite3

from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import JSONResponse

from verify_packet import PACKET_LEN, PacketRejected, verify_and_decrypt

# The real offline application may upload days after ECU detection. This demo
# allows up to 30 days of offline backlog, and 5 minutes of future clock drift.
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


def record_report(db_path: Path, decoded: dict, packet: bytes) -> str:
    """Return 'accepted' or 'duplicate'; reject ID collision with changed packet.

    INSERT OR IGNORE + PK constraint guarantees that concurrent submissions do
    not create duplicate reports. An identical signed packet is acknowledged as
    a safe transport retry, rather than stored twice.
    """
    packet_hash = hashlib.sha256(packet).hexdigest()
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
        existing = db.execute(
            'SELECT packet_hash FROM reports WHERE device_id = ? AND message_id = ?',
            (decoded['device_id'], decoded['message_id']),
        ).fetchone()
    if existing and existing[0] == packet_hash:
        return 'duplicate'
    raise HTTPException(status_code=409, detail='Replay / conflicting message ID')


def create_app(*, db_path: str | Path | None = None, keys_dir: str | Path | None = None) -> FastAPI:
    database = Path(db_path if db_path is not None else os.getenv('ROVO_CLOUD_DB', 'data/cloud_reports.sqlite3'))
    demo_keys = Path(keys_dir if keys_dir is not None else os.getenv('ROVO_KEYS_DIR', 'keys'))
    init_database(database)
    app = FastAPI(title='ROVO Phase 2 Cloud Simulator', version='0.2.0')

    @app.get('/healthz')
    def healthz():
        return {'status': 'ok', 'service': 'rovo-phase2'}

    @app.post('/v1/hazards')
    async def ingest_hazard(request: Request):
        media_type = request.headers.get('content-type', '').split(';', 1)[0].lower().strip()
        if media_type != 'application/octet-stream':
            raise HTTPException(status_code=415, detail='Use application/octet-stream')
        # Read at most 161 bytes even if a client sends a huge or deceptive body.
        packet = bytearray()
        async for chunk in request.stream():
            packet.extend(chunk)
            if len(packet) > PACKET_LEN:
                raise HTTPException(status_code=413, detail='Packet too large')
        if len(packet) != PACKET_LEN:
            raise HTTPException(status_code=422, detail='Incorrect packet size')

        try:
            decoded = verify_and_decrypt(bytes(packet), demo_keys)
            detected_at = datetime.fromisoformat(decoded['timestamp_utc'])
        except (PacketRejected, OSError, ValueError, OverflowError) as exc:
            # Avoid returning key-path / low-level crypto diagnostics in API responses.
            raise HTTPException(status_code=422, detail='Invalid signed packet') from exc

        now = datetime.now(timezone.utc)
        if detected_at > now + MAX_CLOCK_SKEW:
            raise HTTPException(status_code=422, detail='Timestamp is in the future')
        if detected_at < now - MAX_OFFLINE_AGE:
            raise HTTPException(status_code=422, detail='Timestamp exceeds offline acceptance window')

        try:
            outcome = record_report(database, decoded, bytes(packet))
        except sqlite3.Error as exc:
            raise HTTPException(status_code=503, detail='Database unavailable') from exc
        return JSONResponse(status_code=200 if outcome == 'duplicate' else 201,
                            content={
                                'status': outcome,
                                'device_id': decoded['device_id'],
                                'message_id': decoded['message_id'],
                                'hazard_type': decoded['hazard_type'],
                                'packet_bytes': len(packet),
                            })

    return app


app = create_app()
