"""Phase 2 HTTP simulator, offline delivery, and signed-report tests."""
from datetime import datetime, timedelta, timezone
from pathlib import Path
import sqlite3
import subprocess

import pytest

from mobile_sim import enqueue_packet, queue_status, upload_pending
from test_support import post, sample_packet
from verify_packet import PACKET_LEN, verify_signed_packet


def test_new_packet_accepted_then_identical_retry_acknowledged(setup):
    packet = sample_packet(setup)
    assert verify_signed_packet(packet, setup['key_dir'])['hazard_type'] == 'Pothole'
    assert (post(setup, packet).status_code, post(setup, packet).status_code) == (201, 200)
    with sqlite3.connect(setup['db']) as db:
        assert db.execute('SELECT COUNT(*) FROM reports').fetchone()[0] == 1


def test_same_message_id_but_different_valid_packet_rejected_as_replay(setup):
    packet = sample_packet(setup)
    report = verify_signed_packet(packet, setup['key_dir'])
    assert post(setup, packet).status_code == 201
    # Validly re-signed changed payload with unchanged message ID.
    conflict = sample_packet(setup, message_id=bytes.fromhex(report['message_id']), hazard_type=2)
    assert post(setup, conflict).status_code == 409


@pytest.mark.parametrize('byte_index', [5, 21, 23, 55, 56, 119])
def test_modified_packet_rejected(setup, byte_index):
    packet = bytearray(sample_packet(setup))
    packet[byte_index] ^= 1
    assert post(setup, bytes(packet)).status_code == 422
    with sqlite3.connect(setup['db']) as db:
        assert db.execute('SELECT COUNT(*) FROM reports').fetchone()[0] == 0


def test_bad_body_and_content_type_rejected(setup):
    good = sample_packet(setup)
    wrong_type = setup['client'].post('/v1/hazards', content=good,
                                      headers={'Content-Type': 'application/json'})
    assert wrong_type.status_code == 415
    assert post(setup, good[:-1]).status_code == 422
    assert post(setup, good + b'X').status_code == 413


@pytest.mark.parametrize('delta,allowed', [(-29, True), (-31, False), (1, False)])
def test_offline_timestamp_policy(setup, delta, allowed):
    packet = sample_packet(setup, timestamp=datetime.now(timezone.utc) + timedelta(days=delta))
    assert post(setup, packet).status_code == (201 if allowed else 422)


def test_mobile_offline_storage_and_online_sync(setup):
    one, two = sample_packet(setup), sample_packet(setup)
    assert enqueue_packet(setup['mobile_db'], one) == 'queued'
    assert enqueue_packet(setup['mobile_db'], one) == 'already_queued'
    assert enqueue_packet(setup['mobile_db'], two) == 'queued'
    assert queue_status(setup['mobile_db'])['pending'] == 2
    with sqlite3.connect(setup['mobile_db']) as db:
        stored = [row[0] for row in db.execute('SELECT packet FROM outbox ORDER BY id')]
    assert stored == [one, two]

    def sender(packet):
        response = post(setup, packet)
        return response.status_code, response.json()

    assert upload_pending(setup['mobile_db'], sender)['sent'] == 2
    assert queue_status(setup['mobile_db'])['pending'] == 0
    with sqlite3.connect(setup['db']) as db:
        assert db.execute('SELECT COUNT(*) FROM reports').fetchone()[0] == 2


def test_network_down_keeps_pending_and_retries(setup):
    packet = sample_packet(setup)
    enqueue_packet(setup['mobile_db'], packet)

    def offline(_packet):
        raise OSError('offline')

    assert upload_pending(setup['mobile_db'], offline)['retry_later'] == 1
    assert queue_status(setup['mobile_db'])['pending'] == 1

    def sender(data):
        response = post(setup, data)
        return response.status_code, response.json()

    assert upload_pending(setup['mobile_db'], sender)['sent'] == 1
    assert queue_status(setup['mobile_db'])['pending'] == 0


def test_mobile_retains_rejected_packet_for_diagnosis(setup):
    broken = bytearray(sample_packet(setup))
    broken[80] ^= 1  # signature
    enqueue_packet(setup['mobile_db'], bytes(broken))

    def sender(data):
        response = post(setup, data)
        return response.status_code, response.json()

    assert upload_pending(setup['mobile_db'], sender)['rejected'] == 1
    assert queue_status(setup['mobile_db']) == {'pending': 0, 'rejected': 1}


def test_optional_actual_cpp_binary(setup):
    """Verify real C++/Monocypher signing when ./build.sh was run locally."""
    binary = Path(__file__).resolve().parent / 'build' / 'ecu_sim'
    if not binary.exists():
        pytest.skip('Build ./build/ecu_sim to also test native C++ signing')
    target = setup['tmp_path'] / 'native_packet.bin'
    subprocess.run([str(binary), str(setup['key_dir']), str(target)], check=True)
    generated = target.read_bytes()
    assert len(generated) == PACKET_LEN
    assert post(setup, generated).status_code == 201
