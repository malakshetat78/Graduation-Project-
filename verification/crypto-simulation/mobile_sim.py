"""Phase 3 CLI mobile relay simulator: offline SQLite queue and HTTPS/HTTP upload.
The mobile relays signed, readable hazard bytes without modifying them.
Local demo uses HTTP; production mobile-to-Cloud Run transport MUST use HTTPS.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sqlite3
from typing import Callable
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

from verify_packet import PACKET_LEN

DEFAULT_QUEUE = Path('data/mobile_queue.sqlite3')
DEFAULT_URL = 'http://127.0.0.1:8001/v1/hazards'  # LOCAL SIMULATOR ONLY


def init_queue(db_path: Path) -> None:
    db_path.parent.mkdir(parents=True, exist_ok=True)
    with sqlite3.connect(db_path, timeout=10) as db:
        db.execute("""CREATE TABLE IF NOT EXISTS outbox (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            packet_hash TEXT NOT NULL UNIQUE,
            packet BLOB NOT NULL,
            state TEXT NOT NULL DEFAULT 'pending',
            attempts INTEGER NOT NULL DEFAULT 0,
            last_error TEXT
        )""")


def enqueue_packet(db_path: Path, packet: bytes) -> str:
    """Store raw opaque bytes, with deduplication of the same received packet."""
    init_queue(db_path)
    if len(packet) != PACKET_LEN or packet[:4] != b'ROVO' or packet[4] != 2:
        raise ValueError('Expected a 120-byte ROVO v2 signed packet')
    fingerprint = hashlib.sha256(packet).hexdigest()
    with sqlite3.connect(db_path, timeout=10) as db:
        cursor = db.execute(
            'INSERT OR IGNORE INTO outbox (packet_hash, packet) VALUES (?, ?)',
            (fingerprint, packet),
        )
    return 'queued' if cursor.rowcount == 1 else 'already_queued'


def queue_status(db_path: Path) -> dict:
    init_queue(db_path)
    with sqlite3.connect(db_path, timeout=10) as db:
        rows = db.execute('SELECT state, COUNT(*) FROM outbox GROUP BY state').fetchall()
    counts = {'pending': 0, 'rejected': 0}
    counts.update({state: count for state, count in rows})
    return counts


def http_sender(url: str, timeout: float = 8.0) -> Callable[[bytes], tuple[int, dict]]:
    """Return a function that sends exactly the original binary packet."""
    def send(packet: bytes) -> tuple[int, dict]:
        req = Request(url, data=packet, method='POST',
                      headers={'Content-Type': 'application/octet-stream'})
        try:
            with urlopen(req, timeout=timeout) as response:
                return response.status, json.loads(response.read())
        except HTTPError as exc:
            try:
                body = json.loads(exc.read())
            except (json.JSONDecodeError, UnicodeDecodeError):
                body = {'detail': 'Server rejected request'}
            return exc.code, body
    return send


def upload_pending(db_path: Path, sender: Callable[[bytes], tuple[int, dict]]) -> dict:
    """Upload pending packets; remove only after explicit server acknowledgment.

    Network/5xx failure stays pending for future retries. Permanent 4xx
    rejection stays stored for diagnosis but is no longer auto-retried.
    """
    init_queue(db_path)
    result = {'sent': 0, 'duplicates': 0, 'rejected': 0, 'retry_later': 0}
    with sqlite3.connect(db_path, timeout=10) as db:
        rows = db.execute(
            "SELECT id, packet FROM outbox WHERE state = 'pending' ORDER BY id"
        ).fetchall()
    for packet_id, packet in rows:
        try:
            code, response = sender(bytes(packet))
        except (OSError, URLError, TimeoutError, ValueError) as exc:
            with sqlite3.connect(db_path, timeout=10) as db:
                db.execute('UPDATE outbox SET attempts=attempts+1, last_error=? WHERE id=?',
                           (f'Connection failed: {type(exc).__name__}', packet_id))
            result['retry_later'] += 1
            print('[Mobile] Offline/network unavailable; message retained for retry')
            break  # don't hammer the network for the remaining packets

        if code in (200, 201) and response.get('status') in ('accepted', 'duplicate'):
            with sqlite3.connect(db_path, timeout=10) as db:
                db.execute('DELETE FROM outbox WHERE id=?', (packet_id,))
            key = 'duplicates' if response['status'] == 'duplicate' else 'sent'
            result[key] += 1
            print(f'[Mobile] Cloud ACK: {response["status"]} message_id={response.get("message_id")}')
        elif 400 <= code < 500 and code != 429:
            with sqlite3.connect(db_path, timeout=10) as db:
                db.execute("UPDATE outbox SET attempts=attempts+1, state='rejected', last_error=? WHERE id=?",
                           (f'HTTP {code}: {response.get("detail", "rejected")}', packet_id))
            result['rejected'] += 1
            print(f'[Mobile] Rejected (HTTP {code}); retained for inspection')
        else:
            with sqlite3.connect(db_path, timeout=10) as db:
                db.execute('UPDATE outbox SET attempts=attempts+1, last_error=? WHERE id=?',
                           (f'HTTP {code}', packet_id))
            result['retry_later'] += 1
            print(f'[Mobile] Cloud unavailable (HTTP {code}); message retained')
            break
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description='ROVO offline mobile relay simulator')
    parser.add_argument('--db', default=str(DEFAULT_QUEUE), help='SQLite mobile queue path')
    subs = parser.add_subparsers(dest='command', required=True)
    receiving = subs.add_parser('receive', help='Simulate BLE reception and offline storage')
    receiving.add_argument('packet', help='120-byte signed binary packet generated by C++')
    subs.add_parser('status', help='Show offline queue size')
    syncing = subs.add_parser('sync', help='Upload when internet is available')
    syncing.add_argument('--url', default=DEFAULT_URL)
    args = parser.parse_args()
    db = Path(args.db)
    if args.command == 'receive':
        try:
            raw = Path(args.packet).read_bytes()
            status = enqueue_packet(db, raw)
        except (ValueError, OSError) as exc:
            parser.error(str(exc))
        print(f'[Mobile] {status}: {len(raw)} signed bytes (no message-level encryption)')
    elif args.command == 'status':
        print('[Mobile] queue:', json.dumps(queue_status(db)))
    elif args.command == 'sync':
        stats = upload_pending(db, http_sender(args.url))
        print('[Mobile] sync results:', json.dumps(stats))
        print('[Mobile] remaining queue:', json.dumps(queue_status(db)))


if __name__ == '__main__':
    main()
