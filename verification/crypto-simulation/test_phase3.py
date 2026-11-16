"""Phase 3 security and reliability regression cases for signed-only v2 demo.

Fixture from conftest.py; signed packet helpers from test_support.py.
The isolated temporary key/database never touches the developer's keys/ or data/.
"""
from datetime import timedelta, timezone
import os
from pathlib import Path
import secrets
import sqlite3
import subprocess
import sys

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from fastapi.testclient import TestClient

from cloud_api import create_app
from mobile_sim import enqueue_packet, queue_status, upload_pending
from test_support import post, sample_packet
from verify_packet import PACKET_LEN


def stored_count(db_path: Path) -> int:
    with sqlite3.connect(db_path) as db:
        return db.execute('SELECT COUNT(*) FROM reports').fetchone()[0]


def test_phase3_exact_replay_is_acknowledged_but_not_inserted_twice(setup):
    packet = sample_packet(setup)
    assert (post(setup, packet).status_code, post(setup, packet).status_code) == (201, 200)
    assert stored_count(setup['db']) == 1


def test_phase3_conflicting_message_id_rejected_even_with_valid_signature(setup):
    same_id = secrets.token_bytes(16)
    first = sample_packet(setup, message_id=same_id)
    changed = sample_packet(setup, message_id=same_id, hazard_type=2)
    assert first != changed
    assert post(setup, first).status_code == 201
    assert post(setup, changed).status_code == 409
    assert stored_count(setup['db']) == 1


def test_phase3_fake_ecu_cannot_sign_with_unregistered_private_key(setup):
    authentic = sample_packet(setup)
    bogus = authentic[:-64] + Ed25519PrivateKey.generate().sign(authentic[:-64])
    assert post(setup, bogus).status_code == 422
    assert stored_count(setup['db']) == 0


def test_phase3_unknown_device_id_rejected_even_if_signed_by_valid_key(setup):
    """Change ONLY the device ID and re-sign with the same valid device key."""
    packet = sample_packet(setup, device_id=b'FAKE-DEVICE-0001')
    assert len(packet) == PACKET_LEN
    assert post(setup, packet).status_code == 422
    assert stored_count(setup['db']) == 0


@pytest.mark.parametrize('index', [4, 5, 21, 23, 55, 56, 119])
def test_phase3_header_payload_or_signature_tampering_rejected(setup, index):
    packet = bytearray(sample_packet(setup))
    packet[index] ^= 1
    assert post(setup, bytes(packet)).status_code == 422
    assert stored_count(setup['db']) == 0


def test_phase3_valid_signature_but_invalid_payload_rejected(setup):
    """Authenticated device isn't allowed to send nonsensical coordinates."""
    packet = sample_packet(setup, latitude_e7=1000000000)
    assert post(setup, packet).status_code == 422


def test_phase3_old_accepted_packet_retried_after_31_days(setup, monkeypatch):
    """ACK loss: valid upload at age 29 days, exact retry at age 31 days."""
    import datetime as datetime_module

    observed_now = datetime_module.datetime.now(timezone.utc)
    detected = observed_now - timedelta(days=31)
    packet = sample_packet(setup, timestamp=detected)

    class TwoDaysEarlier(datetime_module.datetime):
        @classmethod
        def now(cls, tz=None):
            return datetime_module.datetime.now(tz) - timedelta(days=2)

    with monkeypatch.context() as patch:
        patch.setattr('cloud_api.datetime', TwoDaysEarlier)
        assert post(setup, packet).status_code == 201  # 29-day delayed upload

    duplicate = post(setup, packet)  # now 31 days late; identical ACK retry
    assert duplicate.status_code == 200
    assert duplicate.json()['status'] == 'duplicate'
    assert post(setup, sample_packet(setup, timestamp=detected)).status_code == 422
    assert stored_count(setup['db']) == 1


def test_phase3_ten_distinct_offline_reports_survive_outage_and_lost_ack(setup):
    packets = [sample_packet(setup) for _ in range(10)]
    for packet in packets:
        assert enqueue_packet(setup['mobile_db'], packet) == 'queued'
    assert queue_status(setup['mobile_db']) == {'pending': 10, 'rejected': 0}

    def offline(_packet):
        raise OSError('Simulated offline')

    assert upload_pending(setup['mobile_db'], offline)['retry_later'] == 1
    assert queue_status(setup['mobile_db'])['pending'] == 10
    assert stored_count(setup['db']) == 0

    def lost_ack(packet):
        assert post(setup, packet).status_code == 201
        raise TimeoutError('Lost ACK after database commit')

    assert upload_pending(setup['mobile_db'], lost_ack)['retry_later'] == 1
    assert queue_status(setup['mobile_db'])['pending'] == 10
    assert stored_count(setup['db']) == 1

    def online(packet):
        response = post(setup, packet)
        return response.status_code, response.json()

    assert upload_pending(setup['mobile_db'], online) == {
        'sent': 9, 'duplicates': 1, 'rejected': 0, 'retry_later': 0,
    }
    assert queue_status(setup['mobile_db']) == {'pending': 0, 'rejected': 0}
    assert stored_count(setup['db']) == 10


def test_phase3_duplicate_ledger_survives_server_restart(setup):
    packet = sample_packet(setup)
    assert post(setup, packet).status_code == 201
    with TestClient(create_app(db_path=setup['db'], keys_dir=setup['key_dir'])) as restarted:
        response = restarted.post('/v1/hazards', content=packet,
                                  headers={'Content-Type': 'application/octet-stream'})
        assert response.status_code == 200
        assert response.json()['status'] == 'duplicate'
    assert stored_count(setup['db']) == 1


def test_phase3_creating_app_does_not_create_database(setup, tmp_path):
    never_initialized = tmp_path / 'not-created' / 'reports.sqlite3'
    create_app(db_path=never_initialized, keys_dir=setup['key_dir'])
    assert not never_initialized.exists()
    assert not never_initialized.parent.exists()
    with TestClient(create_app(db_path=never_initialized, keys_dir=setup['key_dir'])):
        assert never_initialized.is_file()


def test_phase3_import_has_no_database_side_effects(tmp_path):
    project_root = Path(__file__).resolve().parent
    env = dict(os.environ, PYTHONPATH=str(project_root))
    subprocess.run([sys.executable, '-c', 'import cloud_api'], cwd=tmp_path,
                   env=env, check=True, capture_output=True)
    assert not (tmp_path / 'data').exists()


def test_phase3_real_cpp_packet_to_python_when_built(setup):
    binary = Path(__file__).resolve().parent / 'build' / 'ecu_sim'
    if not binary.is_file():
        pytest.skip('C++/Monocypher binary missing: first run ./build.sh')
    packet_file = setup['tmp_path'] / 'signed_from_cpp.bin'
    subprocess.run([str(binary), str(setup['key_dir']), str(packet_file)],
                   check=True, capture_output=True, text=True)
    assert len(packet_file.read_bytes()) == PACKET_LEN
    assert post(setup, packet_file.read_bytes()).status_code == 201
    assert stored_count(setup['db']) == 1
