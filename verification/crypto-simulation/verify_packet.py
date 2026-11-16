"""Standalone ROVO v2 signed-report demo (NOT the project's ICVSP wire format).

Header: magic[4] + version[1] + device_id[16] + data_len[2] = 23 bytes.
Payload: message_id[16] + timestamp_ms[8] + lat_e7[4] + lon_e7[4] + hazard[1].
Signature: Ed25519 over the entire header and payload (64 bytes).
No message-level encryption: TLS is required for production mobile-to-cloud traffic.
"""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import struct

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

HEADER_LEN = 23
PAYLOAD_LEN = 33
SIGNATURE_LEN = 64
PACKET_LEN = HEADER_LEN + PAYLOAD_LEN + SIGNATURE_LEN
DEVICE_ID = b'ROVO-DEMO-ECU-01'
VERSION = 2


class PacketRejected(Exception):
    """Invalid, untrusted, or unsupported signed report."""


def read_key(keys_dir: Path, name: str, expected_length: int) -> bytes:
    data = (keys_dir / name).read_bytes()
    if len(data) != expected_length:
        raise PacketRejected(f'Bad key file: {name}')
    return data


def verify_signed_packet(packet: bytes, keys_dir: Path) -> dict:
    """Verify signature first, then decode fixed-layout hazard fields.

    One hard-coded demo device ID/public key. This is NOT a device registry.
    """
    if len(packet) != PACKET_LEN:
        raise PacketRejected(f'Wrong packet length: {len(packet)} (expected {PACKET_LEN})')
    if packet[:4] != b'ROVO' or packet[4] != VERSION:
        raise PacketRejected('Bad magic or packet version')
    if packet[5:21] != DEVICE_ID:
        raise PacketRejected('Unknown device ID')
    if int.from_bytes(packet[21:23], 'big') != PAYLOAD_LEN:
        raise PacketRejected('Unsupported payload length')

    signed_data, signature = packet[:-SIGNATURE_LEN], packet[-SIGNATURE_LEN:]
    pub = read_key(keys_dir, 'sign_pub.bin', 32)
    try:
        Ed25519PublicKey.from_public_bytes(pub).verify(signature, signed_data)
    except (InvalidSignature, ValueError) as exc:
        raise PacketRejected('Ed25519 signature invalid') from exc

    message_id, timestamp_ms, latitude_e7, longitude_e7, hazard_type = struct.unpack(
        '>16sQiiB', packet[HEADER_LEN:-SIGNATURE_LEN]
    )
    if not (-900_000_000 <= latitude_e7 <= 900_000_000):
        raise PacketRejected('Bad latitude')
    if not (-1_800_000_000 <= longitude_e7 <= 1_800_000_000):
        raise PacketRejected('Bad longitude')
    if hazard_type not in (1, 2):
        raise PacketRejected('Unknown hazard type')
    try:
        timestamp = datetime.fromtimestamp(timestamp_ms / 1000, timezone.utc).isoformat()
    except (OverflowError, OSError, ValueError) as exc:
        raise PacketRejected('Invalid timestamp') from exc
    return {
        'device_id': DEVICE_ID.decode('ascii'),
        'message_id': message_id.hex(),
        'timestamp_utc': timestamp,
        'latitude': latitude_e7 / 10_000_000,
        'longitude': longitude_e7 / 10_000_000,
        'hazard_type': {1: 'Pothole', 2: 'Speed bump'}[hazard_type],
        'packet_bytes': len(packet),
    }


def main():
    parser = argparse.ArgumentParser(description='Verify a ROVO v2 signed packet')
    parser.add_argument('packet', nargs='?', default='packet.bin')
    parser.add_argument('--keys', default='keys')
    args = parser.parse_args()
    try:
        result = verify_signed_packet(Path(args.packet).read_bytes(), Path(args.keys))
    except (PacketRejected, OSError) as exc:
        raise SystemExit(f'[Cloud Python] REJECTED: {exc}') from exc
    print('[Cloud Python] Ed25519 signature: VALID')
    for name, value in result.items():
        print(f'[Cloud Python] {name}: {value}')
    print('[Cloud Python] PASS — ECU and Cloud agree on signed packet v2')


if __name__ == '__main__':
    main()
