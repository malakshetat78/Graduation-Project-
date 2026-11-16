"""Phase 1 standalone Ed25519-only packet verifier checks."""

import pytest

from test_support import sample_packet
from verify_packet import PACKET_LEN, PacketRejected, verify_signed_packet


def test_phase1_valid_signed_packet(setup):
    packet = sample_packet(setup)
    decoded = verify_signed_packet(packet, setup['key_dir'])
    assert len(packet) == PACKET_LEN == 120
    assert decoded['hazard_type'] == 'Pothole'
    assert decoded['device_id'] == 'ROVO-DEMO-ECU-01'


@pytest.mark.parametrize('index', [4, 5, 21, 23, 34, 55, 56, 119])
def test_phase1_tampering_rejected(setup, index):
    tampered = bytearray(sample_packet(setup))
    tampered[index] ^= 1
    with pytest.raises(PacketRejected):
        verify_signed_packet(bytes(tampered), setup['key_dir'])
