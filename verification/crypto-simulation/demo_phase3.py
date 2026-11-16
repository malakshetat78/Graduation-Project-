"""Interactive Phase 3 local simulator (actual C++ binary -> mobile queue -> FastAPI).

Run within ROVO_Phase2: python demo_phase3.py
Does NOT modify your existing cloud/mobile SQLite databases, packet.bin, or keys.
The HTTP calls are in-process via FastAPI TestClient, NOT actual network/TLS.
"""
from __future__ import annotations

from datetime import datetime
from pathlib import Path
import secrets
import sqlite3
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from fastapi.testclient import TestClient
from nacl.bindings import crypto_aead_xchacha20poly1305_ietf_encrypt

from cloud_api import create_app
from mobile_sim import enqueue_packet, queue_status, upload_pending
from verify_packet import PACKET_LEN, verify_and_decrypt

PROJECT = Path(__file__).resolve().parent
DEVICE_ID = b'ROVO-DEMO-ECU-01'


def count_reports(db_path: Path) -> int:
    with sqlite3.connect(db_path) as db:
        return db.execute('SELECT COUNT(*) FROM reports').fetchone()[0]


def make_conflicting_valid_packet(existing_packet: bytes, keys_dir: Path) -> bytes:
    """Make a second correctly signed/encrypted demo packet with the same message ID.

    This is an insider/key-holder simulation to test the message-ID uniqueness rule,
    NOT proof that an outside attacker can forge a valid signature.
    """
    decoded = verify_and_decrypt(existing_packet, keys_dir)
    ms = round(datetime.fromisoformat(decoded['timestamp_utc']).timestamp() * 1000)
    hazard_code = 1 if decoded['hazard_type'] == 'Pothole' else 2
    plaintext = struct.pack(
        '>16sQiiB', bytes.fromhex(decoded['message_id']), ms,
        round(decoded['latitude'] * 10_000_000),
        round(decoded['longitude'] * 10_000_000), hazard_code,
    )
    nonce = secrets.token_bytes(24)
    header = b'ROVO' + b'\x01' + DEVICE_ID + nonce + struct.pack('>H', 33)
    encryption_key = (keys_dir / 'enc_key.bin').read_bytes()
    signing_seed = (keys_dir / 'sign_seed.bin').read_bytes()
    cipher_tag = crypto_aead_xchacha20poly1305_ietf_encrypt(plaintext, header, nonce, encryption_key)
    signed_data = header + cipher_tag
    signature = Ed25519PrivateKey.from_private_bytes(signing_seed).sign(signed_data)
    packet = signed_data + signature
    assert len(packet) == PACKET_LEN
    return packet


def main() -> int:
    binary = PROJECT / 'build' / 'ecu_sim'
    keys = PROJECT / 'keys'
    if not binary.is_file():
        print('ERROR: C++ binary missing. Run ./build.sh inside ROVO_Phase2 first.')
        return 2
    if not all((keys / name).is_file() for name in ('enc_key.bin', 'sign_seed.bin', 'sign_pub.bin')):
        print('ERROR: demo keys missing. Use your existing keys/ or run generate_keys.py ONCE.')
        return 2

    # Always isolate Phase 3 demo traffic from Phase 2 databases and packet.bin.
    with TemporaryDirectory(prefix='rovo_phase3_') as tmp_name:
        tmp = Path(tmp_name)
        db = tmp / 'reports.sqlite3'
        queue = tmp / 'outbox.sqlite3'
        client = TestClient(create_app(db_path=db, keys_dir=keys))
        def post(packet: bytes):
            return client.post('/v1/hazards', content=packet,
                               headers={'Content-Type': 'application/octet-stream'})
        try:
            print('\n[1/6] Generating 10 REAL C++/Monocypher ECU packets...')
            packets: list[bytes] = []
            for i in range(10):
                path = tmp / f'packet_{i+1}.bin'
                subprocess.run([str(binary), str(keys.resolve()), str(path)],
                               check=True, text=True, capture_output=True)
                packet = path.read_bytes()
                assert len(packet) == PACKET_LEN
                packets.append(packet)
            decoded_ids = [verify_and_decrypt(p, keys)['message_id'] for p in packets]
            assert len(set(decoded_ids)) == 10
            print('PASS: 10 unique, signed + encrypted 160-byte packets')

            print('\n[2/6] Mobile stores all 10 packets offline...')
            for packet in packets:
                assert enqueue_packet(queue, packet) == 'queued'
            assert queue_status(queue)['pending'] == 10
            def offline(_):
                raise OSError('Simulated offline')
            outcome = upload_pending(queue, offline)
            assert outcome['retry_later'] == 1
            assert queue_status(queue)['pending'] == 10
            print('PASS: offline retry preserved 10/10 packets')

            print('\n[3/6] Simulating server COMMIT but lost ACK...')
            def lost_ack(packet):
                response = post(packet)
                assert response.status_code == 201
                raise TimeoutError('Simulated ACK loss')
            assert upload_pending(queue, lost_ack)['retry_later'] == 1
            assert queue_status(queue)['pending'] == 10
            assert count_reports(db) == 1
            print('PASS: server stored 1; mobile still has 10 (safe for retry)')

            print('\n[4/6] Internet back: mobile resends 10 packets...')
            def sender(packet):
                response = post(packet)
                return response.status_code, response.json()
            uploaded = upload_pending(queue, sender)
            assert uploaded == {'sent': 9, 'duplicates': 1, 'rejected': 0, 'retry_later': 0}, uploaded
            assert queue_status(queue)['pending'] == 0
            assert count_reports(db) == 10
            print('PASS: 9 new + 1 duplicate acknowledged; 10 unique cloud reports')

            print('\n[5/6] Security: replay, fake signature, tampered bytes, conflicting ID...')
            replay = post(packets[0])
            assert replay.status_code == 200 and replay.json()['status'] == 'duplicate'
            fake = packets[0][:-64] + Ed25519PrivateKey.generate().sign(packets[0][:-64])
            assert post(fake).status_code == 422
            edited = bytearray(packets[0]); edited[52] ^= 0x01
            assert post(bytes(edited)).status_code == 422
            conflict = make_conflicting_valid_packet(packets[0], keys)
            assert conflict != packets[0]
            assert post(conflict).status_code == 409
            assert count_reports(db) == 10
            print('PASS: replay idempotent, fake signature rejected, tamper rejected, ID conflict 409')

            print('\n[6/6] Summary')
            print(f'PASS: {len(packets)} ECU packets; {count_reports(db)} unique reports; '
                  f'{queue_status(queue)["pending"]} pending; 0 duplicates stored')
            print('NOTE: FastAPI TestClient is IN-PROCESS, not a real HTTPS/TLS test.')
            print('NOTE: These are security behavior checks, not proof of production security.')
            return 0
        finally:
            client.close()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (AssertionError, OSError, subprocess.CalledProcessError) as exc:
        print(f'FAIL: {exc}', file=sys.stderr)
        sys.exit(1)
