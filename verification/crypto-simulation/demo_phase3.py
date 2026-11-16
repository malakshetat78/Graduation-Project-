"""Actual C++ signed packets -> offline SQLite queue -> in-process FastAPI.

No network/TLS or real ECU/BLE is exercised by this standalone laptop demo.
"""
from pathlib import Path
import sqlite3
import subprocess
import sys
from tempfile import TemporaryDirectory

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from fastapi.testclient import TestClient

from cloud_api import create_app
from mobile_sim import enqueue_packet, queue_status, upload_pending
from verify_packet import PACKET_LEN, verify_signed_packet

PROJECT = Path(__file__).resolve().parent


def count_reports(db_path: Path) -> int:
    with sqlite3.connect(db_path) as db:
        return db.execute('SELECT COUNT(*) FROM reports').fetchone()[0]


def make_conflicting_valid_packet(original: bytes, seed: bytes) -> bytes:
    signed_data = bytearray(original[:-64])
    signed_data[55] = 2 if signed_data[55] == 1 else 1  # hazard code, same message ID
    key = Ed25519PrivateKey.from_private_bytes(seed)
    changed = bytes(signed_data)
    return changed + key.sign(changed)


def main() -> int:
    binary = PROJECT / 'build' / 'ecu_sim'
    keys = PROJECT / 'keys'
    if not binary.is_file():
        print('ERROR: C++ binary missing. Run ./build.sh first.')
        return 2
    if not all((keys / f).is_file() for f in ('sign_seed.bin', 'sign_pub.bin')):
        print('ERROR: signing keys missing. Run python generate_keys.py once.')
        return 2

    with TemporaryDirectory(prefix='rovo_signed_phase3_') as tmp_name:
        tmp = Path(tmp_name)
        db, queue = tmp / 'reports.sqlite3', tmp / 'mobile.sqlite3'
        with TestClient(create_app(db_path=db, keys_dir=keys)) as client:
            def post(packet):
                return client.post('/v1/hazards', content=packet,
                                   headers={'Content-Type': 'application/octet-stream'})

            packets = []
            print('[1/6] Generating 10 real C++ signed-only packets...')
            for i in range(10):
                path = tmp / f'packet_{i}.bin'
                subprocess.run([str(binary), str(keys.resolve()), str(path)],
                               check=True, capture_output=True, text=True)
                packet = path.read_bytes()
                assert len(packet) == PACKET_LEN == 120
                packets.append(packet)
            ids = [verify_signed_packet(p, keys)['message_id'] for p in packets]
            assert len(set(ids)) == 10
            print('PASS: 10 distinct 120-byte signed C++ packets')

            print('[2/6] Mobile offline queue...')
            for packet in packets:
                assert enqueue_packet(queue, packet) == 'queued'

            def offline(_packet):
                raise OSError('Simulated offline')

            assert upload_pending(queue, offline)['retry_later'] == 1
            assert queue_status(queue)['pending'] == 10

            print('[3/6] Cloud commits, but ACK is lost...')
            def lost_ack(packet):
                assert post(packet).status_code == 201
                raise TimeoutError('Simulated lost ACK')
            assert upload_pending(queue, lost_ack)['retry_later'] == 1
            assert count_reports(db) == 1

            print('[4/6] Network restored, retry all 10...')
            def online(packet):
                response = post(packet)
                return response.status_code, response.json()
            assert upload_pending(queue, online) == {
                'sent': 9, 'duplicates': 1, 'rejected': 0, 'retry_later': 0,
            }
            assert queue_status(queue)['pending'] == 0
            assert count_reports(db) == 10

            print('[5/6] Verify replay/tampering/invalid signature/conflicting ID...')
            assert post(packets[0]).json()['status'] == 'duplicate'
            bogus = packets[0][:-64] + Ed25519PrivateKey.generate().sign(packets[0][:-64])
            assert post(bogus).status_code == 422
            tampered = bytearray(packets[0]); tampered[40] ^= 1
            assert post(bytes(tampered)).status_code == 422
            conflict = make_conflicting_valid_packet(packets[0], (keys / 'sign_seed.bin').read_bytes())
            assert post(conflict).status_code == 409

            print('[6/6] PASS: 10 signed reports; 0 pending; 0 duplicates stored')
            print('NOTE: FastAPI TestClient is in-process; HTTPS, BLE, ESP32 not tested.')
            return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (AssertionError, OSError, subprocess.CalledProcessError) as exc:
        print(f'FAIL: {exc}', file=sys.stderr)
        sys.exit(1)
