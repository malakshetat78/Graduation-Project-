"""Independent Python signing and API helpers used by both test phases.

The signature is computed independently from the C++/Monocypher demo.
"""
from datetime import datetime, timezone
import secrets
import struct

from verify_packet import DEVICE_ID, PACKET_LEN, PAYLOAD_LEN, VERSION


def sample_packet(s, *, message_id=None, timestamp=None, device_id=DEVICE_ID,
                  latitude_e7=300444200, longitude_e7=312357120, hazard_type=1) -> bytes:
    mid = message_id if message_id is not None else secrets.token_bytes(16)
    when = timestamp if timestamp is not None else datetime.now(timezone.utc)
    ts_ms = int(when.timestamp() * 1000)
    payload = struct.pack('>16sQiiB', mid, ts_ms, latitude_e7, longitude_e7, hazard_type)
    header = b'ROVO' + bytes([VERSION]) + device_id + struct.pack('>H', PAYLOAD_LEN)
    signed_data = header + payload
    packet = signed_data + s['signing_key'].sign(signed_data)
    assert len(packet) == PACKET_LEN == 120
    return packet


def post(s, packet):
    return s['client'].post('/v1/hazards', content=packet,
                            headers={'Content-Type': 'application/octet-stream'})
