"""ROVO Phase 3 security + reliability regression tests.

Run from the existing ROVO_Phase2 project folder:
    python -m pytest -v test_phase3.py

Uses isolated temporary demo keys and databases. Never writes your keys/ or data/.
"""
from __future__ import annotations

from pathlib import Path
import secrets
import sqlite3
import subprocess

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from fastapi.testclient import TestClient

from cloud_api import create_app
from mobile_sim import enqueue_packet, queue_status, upload_pending
from test_phase2 import post, sample_packet, setup  # Phase 2's temporary-key fixture
from verify_packet import PACKET_LEN


def stored_count(db_path: Path) -> int:
    with sqlite3.connect(db_path) as db:
        return db.execute('SELECT COUNT(*) FROM reports').fetchone()[0]


def test_phase3_exact_replay_is_acknowledged_but_not_inserted_twice(setup):
    packet = sample_packet(setup)
    first = post(setup, packet)
    replay = post(setup, packet)
    assert (first.status_code, first.json()['status']) == (201, 'accepted')
    assert (replay.status_code, replay.json()['status']) == (200, 'duplicate')
    assert stored_count(setup['db']) == 1


def test_phase3_conflicting_message_id_rejected_even_with_valid_signature(setup):
    same_id = secrets.token_bytes(16)
    first = sample_packet(setup, message_id=same_id)
    different_valid_packet = sample_packet(setup, message_id=same_id)
    assert first != different_valid_packet
    assert post(setup, first).status_code == 201
    conflict = post(setup, different_valid_packet)
    assert conflict.status_code == 409
    assert stored_count(setup['db']) == 1


def test_phase3_fake_ecu_cannot_sign_with_unregistered_private_key(setup):
    authentic = sample_packet(setup)
    attacker_private = Ed25519PrivateKey.generate()
    forged = authentic[:-64] + attacker_private.sign(authentic[:-64])
    assert forged != authentic
    response = post(setup, forged)
    assert response.status_code == 422
    assert stored_count(setup['db']) == 0


def test_phase3_unknown_device_id_rejected_even_if_resigned(setup):
    packet = bytearray(sample_packet(setup))
    packet[5:21] = b'FAKE-DEVICE-0001'  # 16-byte unknown device ID
    # Simulate somebody with a different signing key trying to claim a new device.
    packet[-64:] = Ed25519PrivateKey.generate().sign(bytes(packet[:-64]))
    assert len(packet) == PACKET_LEN
    assert post(setup, bytes(packet)).status_code == 422
    assert stored_count(setup['db']) == 0


@pytest.mark.parametrize('index', [5, 21, 47, 80, 96, 159])
def test_phase3_any_selected_header_or_payload_tampering_rejected(setup, index):
    packet = bytearray(sample_packet(setup))
    packet[index] ^= 0x01
    assert post(setup, bytes(packet)).status_code == 422
    assert stored_count(setup['db']) == 0


def test_phase3_aead_tag_rejects_tamper_even_if_signature_is_valid(setup):
    """Defense-in-depth demo: mutate ciphertext, then re-sign with fixture's valid key.

    The test knows demo signing keys deliberately; this is NOT an attacker without
    private-key access. It isolates the independent AEAD integrity check.
    """
    packet = bytearray(sample_packet(setup))
    packet[55] ^= 0x10  # ciphertext modified
    packet[-64:] = setup['signing_key'].sign(bytes(packet[:-64]))
    assert post(setup, bytes(packet)).status_code == 422
    assert stored_count(setup['db']) == 0


def test_phase3_ten_distinct_offline_reports_survive_outage_and_lost_ack(setup):
    s = setup
    packets = [sample_packet(s) for _ in range(10)]
    assert len(set(packets)) == 10
    for packet in packets:
        assert enqueue_packet(s['mobile_db'], packet) == 'queued'
    assert queue_status(s['mobile_db']) == {'pending': 10, 'rejected': 0}

    def offline(_packet):
        raise OSError('Simulated offline mobile')

    assert upload_pending(s['mobile_db'], offline)['retry_later'] == 1
    assert queue_status(s['mobile_db'])['pending'] == 10
    assert stored_count(s['db']) == 0

    # The server commits the first report, but its ACK never reaches mobile.
    def ack_lost(packet):
        response = post(s, packet)
        assert response.status_code == 201
        raise TimeoutError('Simulated lost ACK after server commit')

    assert upload_pending(s['mobile_db'], ack_lost)['retry_later'] == 1
    assert queue_status(s['mobile_db'])['pending'] == 10
    assert stored_count(s['db']) == 1

    def online(packet):
        response = post(s, packet)
        return response.status_code, response.json()

    uploaded = upload_pending(s['mobile_db'], online)
    assert uploaded == {'sent': 9, 'duplicates': 1, 'rejected': 0, 'retry_later': 0}
    assert queue_status(s['mobile_db']) == {'pending': 0, 'rejected': 0}
    assert stored_count(s['db']) == 10
    assert upload_pending(s['mobile_db'], online)['sent'] == 0
    assert stored_count(s['db']) == 10


def test_phase3_duplicate_ledger_survives_server_restart(setup):
    packet = sample_packet(setup)
    assert post(setup, packet).status_code == 201
    restarted = TestClient(create_app(db_path=setup['db'], keys_dir=setup['key_dir']))
    try:
        response = restarted.post('/v1/hazards', content=packet,
                                  headers={'Content-Type': 'application/octet-stream'})
        assert response.status_code == 200
        assert response.json()['status'] == 'duplicate'
    finally:
        restarted.close()
    assert stored_count(setup['db']) == 1


def test_phase3_real_cpp_ecu_packet_to_python_cloud_when_built(setup):
    binary = Path(__file__).resolve().parent / 'build' / 'ecu_sim'
    if not binary.is_file():
        pytest.skip('No built ECU simulator: first run ./build.sh')
    packet_file = setup['tmp_path'] / 'cpp_phase3.bin'
    subprocess.run([str(binary), str(setup['key_dir']), str(packet_file)],
                   check=True, capture_output=True, text=True)
    assert len(packet_file.read_bytes()) == PACKET_LEN
    response = post(setup, packet_file.read_bytes())
    assert response.status_code == 201
    assert stored_count(setup['db']) == 1
