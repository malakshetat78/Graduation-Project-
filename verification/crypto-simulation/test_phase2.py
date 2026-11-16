"""Automated Phase 2 tests. Run: python -m pytest -v test_phase2.py"""
from datetime import datetime, timedelta, timezone
from pathlib import Path
import secrets
import sqlite3
import struct
import subprocess

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from fastapi.testclient import TestClient
from nacl.bindings import crypto_aead_xchacha20poly1305_ietf_encrypt

from cloud_api import create_app
from mobile_sim import enqueue_packet, queue_status, upload_pending
from verify_packet import PACKET_LEN, verify_and_decrypt

DEVICE_ID = b'ROVO-DEMO-ECU-01'


@pytest.fixture
def setup(tmp_path):
    key_dir = tmp_path / 'keys'
    key_dir.mkdir()
    key = secrets.token_bytes(32)
    seed = secrets.token_bytes(32)
    signing_key = Ed25519PrivateKey.from_private_bytes(seed)
    public = signing_key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
    (key_dir / 'enc_key.bin').write_bytes(key)
    (key_dir / 'sign_seed.bin').write_bytes(seed)
    (key_dir / 'sign_pub.bin').write_bytes(public)
    db = tmp_path / 'cloud.sqlite3'
    mobile_db = tmp_path / 'mobile.sqlite3'
    client = TestClient(create_app(db_path=db, keys_dir=key_dir))
    return {'tmp_path': tmp_path, 'key_dir': key_dir, 'key': key, 'signing_key': signing_key,
            'client': client, 'db': db, 'mobile_db': mobile_db}


def sample_packet(s, *, message_id=None, timestamp=None, nonce=None) -> bytes:
    """Construct format v1 using independent Python/libsodium test implementation."""
    mid = message_id if message_id is not None else secrets.token_bytes(16)
    when = timestamp if timestamp is not None else datetime.now(timezone.utc)
    ts_ms = int(when.timestamp() * 1000)
    plaintext = struct.pack('>16sQiiB', mid, ts_ms, 300444200, 312357120, 1)
    nonce = nonce if nonce is not None else secrets.token_bytes(24)
    header = b'ROVO' + b'\x01' + DEVICE_ID + nonce + struct.pack('>H', len(plaintext))
    ciphertext_and_tag = crypto_aead_xchacha20poly1305_ietf_encrypt(
        plaintext, header, nonce, s['key'])
    signed_data = header + ciphertext_and_tag
    signature = s['signing_key'].sign(signed_data)
    packet = signed_data + signature
    assert len(packet) == PACKET_LEN == 160
    return packet


def post(s, packet):
    return s['client'].post('/v1/hazards', content=packet,
                            headers={'Content-Type': 'application/octet-stream'})


def test_new_packet_accepted_then_identical_retry_acknowledged(setup):
    s = setup
    packet = sample_packet(s)
    assert verify_and_decrypt(packet, s['key_dir'])['hazard_type'] == 'Pothole'
    first = post(s, packet)
    assert first.status_code == 201
    assert first.json()['status'] == 'accepted'
    second = post(s, packet)
    assert second.status_code == 200
    assert second.json()['status'] == 'duplicate'
    with sqlite3.connect(s['db']) as db:
        assert db.execute('SELECT COUNT(*) FROM reports').fetchone()[0] == 1


def test_same_message_id_but_different_valid_packet_rejected_as_replay(setup):
    s = setup
    message_id = secrets.token_bytes(16)
    assert post(s, sample_packet(s, message_id=message_id)).status_code == 201
    # Newly signed and encrypted, but uses SAME message ID.
    conflict = post(s, sample_packet(s, message_id=message_id))
    assert conflict.status_code == 409
    with sqlite3.connect(s['db']) as db:
        assert db.execute('SELECT COUNT(*) FROM reports').fetchone()[0] == 1


@pytest.mark.parametrize('byte_index', [21, 47, 80, 96])
def test_modified_packet_rejected(setup, byte_index):
    s = setup
    packet = bytearray(sample_packet(s))
    packet[byte_index] ^= 0x01
    response = post(s, bytes(packet))
    assert response.status_code == 422
    with sqlite3.connect(s['db']) as db:
        assert db.execute('SELECT COUNT(*) FROM reports').fetchone()[0] == 0


def test_bad_body_and_content_type_rejected(setup):
    s = setup
    good = sample_packet(s)
    wrong_type = s['client'].post('/v1/hazards', content=good,
                                   headers={'Content-Type': 'application/json'})
    assert wrong_type.status_code == 415
    assert post(s, good[:-1]).status_code == 422
    assert post(s, good + b'X').status_code == 413


@pytest.mark.parametrize('delta,allowed', [(-29, True), (-31, False), (1, False)])
def test_offline_timestamp_policy(setup, delta, allowed):
    packet = sample_packet(setup, timestamp=datetime.now(timezone.utc) + timedelta(days=delta))
    response = post(setup, packet)
    assert response.status_code == (201 if allowed else 422)


def test_mobile_offline_storage_and_online_sync(setup):
    s = setup
    one, two = sample_packet(s), sample_packet(s)
    assert enqueue_packet(s['mobile_db'], one) == 'queued'
    assert enqueue_packet(s['mobile_db'], one) == 'already_queued'
    assert enqueue_packet(s['mobile_db'], two) == 'queued'
    assert queue_status(s['mobile_db'])['pending'] == 2

    # Before network is available, verify raw encrypted packets remain unchanged.
    with sqlite3.connect(s['mobile_db']) as db:
        stored = [row[0] for row in db.execute('SELECT packet FROM outbox ORDER BY id')]
    assert stored == [one, two]

    def sender(packet):
        response = post(s, packet)
        return response.status_code, response.json()

    results = upload_pending(s['mobile_db'], sender)
    assert results['sent'] == 2
    assert queue_status(s['mobile_db'])['pending'] == 0
    with sqlite3.connect(s['db']) as db:
        assert db.execute('SELECT COUNT(*) FROM reports').fetchone()[0] == 2


def test_network_down_keeps_pending_and_retries(setup):
    s = setup
    packet = sample_packet(s)
    enqueue_packet(s['mobile_db'], packet)

    def down(_):
        raise OSError('offline')

    assert upload_pending(s['mobile_db'], down)['retry_later'] == 1
    assert queue_status(s['mobile_db'])['pending'] == 1

    def sender(data):
        response = post(s, data)
        return response.status_code, response.json()

    assert upload_pending(s['mobile_db'], sender)['sent'] == 1
    assert queue_status(s['mobile_db'])['pending'] == 0


def test_mobile_retains_rejected_packet_for_diagnosis(setup):
    s = setup
    broken = bytearray(sample_packet(s))
    broken[120] ^= 1  # signature
    enqueue_packet(s['mobile_db'], bytes(broken))

    def sender(data):
        response = post(s, data)
        return response.status_code, response.json()

    assert upload_pending(s['mobile_db'], sender)['rejected'] == 1
    assert queue_status(s['mobile_db'])['rejected'] == 1
    assert queue_status(s['mobile_db'])['pending'] == 0


def test_optional_actual_cpp_binary(setup):
    """Real Monocypher/C++ interoperability when user has run ./build.sh."""
    binary = Path(__file__).resolve().parent / 'build' / 'ecu_sim'
    if not binary.exists():
        pytest.skip('Build ./build/ecu_sim to also test real native C++ packet')
    target = setup['tmp_path'] / 'native_packet.bin'
    subprocess.run([str(binary), str(setup['key_dir']), str(target)], check=True)
    generated = target.read_bytes()
    assert len(generated) == PACKET_LEN
    assert post(setup, generated).status_code == 201
