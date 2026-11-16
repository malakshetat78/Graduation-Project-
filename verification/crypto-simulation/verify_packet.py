"""ROVO Phase 1 Python Cloud simulator: verify Ed25519, then decrypt XChaCha20.
No HTTP, database, key vault, or replay-prevention storage in Phase 1.
"""
import argparse
from datetime import datetime, timezone
from pathlib import Path
import struct

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey
from nacl.bindings import crypto_aead_xchacha20poly1305_ietf_decrypt
from nacl.exceptions import CryptoError

HEADER_LEN = 47
PLAINTEXT_LEN = 33
TAG_LEN = 16
SIGNATURE_LEN = 64
PACKET_LEN = HEADER_LEN + PLAINTEXT_LEN + TAG_LEN + SIGNATURE_LEN
DEVICE_ID = b'ROVO-DEMO-ECU-01'  # Registry lookup would select keys per device in production.


class PacketRejected(Exception):
    pass


def read_key(keys_dir: Path, name: str, expected_length: int) -> bytes:
    data = (keys_dir / name).read_bytes()
    if len(data) != expected_length:
        raise PacketRejected(f'Bad key file: {name}')
    return data


def verify_and_decrypt(packet: bytes, keys_dir: Path) -> dict:
    # Exact length is intentional for the v1 demo; do not allocate from untrusted length.
    if len(packet) != PACKET_LEN:
        raise PacketRejected(f'Wrong packet length: got {len(packet)}, expected {PACKET_LEN}')
    header = packet[:HEADER_LEN]
    if header[:4] != b'ROVO' or header[4] != 1:
        raise PacketRejected('Bad magic or version')
    if header[5:21] != DEVICE_ID:
        raise PacketRejected('Unknown device ID')

    nonce = header[21:45]
    ciphertext_len = int.from_bytes(header[45:47], 'big')
    if ciphertext_len != PLAINTEXT_LEN:
        raise PacketRejected('Unexpected plaintext length for packet v1')
    ciphertext = packet[HEADER_LEN:HEADER_LEN + ciphertext_len]
    tag = packet[HEADER_LEN + ciphertext_len:HEADER_LEN + ciphertext_len + TAG_LEN]
    signature = packet[-SIGNATURE_LEN:]
    signed_data = packet[:-SIGNATURE_LEN]

    # Step 1: verify signature on header + ciphertext + tag (before decryption).
    public_key_bytes = read_key(keys_dir, 'sign_pub.bin', 32)
    try:
        Ed25519PublicKey.from_public_bytes(public_key_bytes).verify(signature, signed_data)
    except InvalidSignature as exc:
        raise PacketRejected('Ed25519 signature invalid') from exc

    # Step 2: decrypt; header is AAD, so the tag also authenticates the header.
    symmetric_key = read_key(keys_dir, 'enc_key.bin', 32)
    try:
        plaintext = crypto_aead_xchacha20poly1305_ietf_decrypt(
            ciphertext + tag, header, nonce, symmetric_key
        )
    except (CryptoError, ValueError) as exc:
        raise PacketRejected('XChaCha20-Poly1305 authentication failed') from exc
    if len(plaintext) != PLAINTEXT_LEN:
        raise PacketRejected('Decrypted size mismatch')

    message_id, timestamp_ms, latitude_e7, longitude_e7, hazard_type = struct.unpack(
        '>16sQiiB', plaintext
    )
    if not (-900_000_000 <= latitude_e7 <= 900_000_000):
        raise PacketRejected('Bad latitude')
    if not (-1_800_000_000 <= longitude_e7 <= 1_800_000_000):
        raise PacketRejected('Bad longitude')
    if hazard_type not in (1, 2):
        raise PacketRejected('Unknown hazard type')

    return {
        'device_id': header[5:21].decode('ascii'),
        'message_id': message_id.hex(),
        'timestamp_utc': datetime.fromtimestamp(timestamp_ms / 1000, timezone.utc).isoformat(),
        'latitude': latitude_e7 / 10_000_000,
        'longitude': longitude_e7 / 10_000_000,
        'hazard_type': {1: 'Pothole', 2: 'Speed bump'}[hazard_type],
        'packet_bytes': len(packet),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('packet', nargs='?', default='packet.bin')
    parser.add_argument('--keys', default='keys')
    args = parser.parse_args()
    try:
        result = verify_and_decrypt(Path(args.packet).read_bytes(), Path(args.keys))
    except (PacketRejected, OSError, OverflowError, ValueError) as exc:
        raise SystemExit(f'[Cloud Python] REJECTED: {exc}') from exc
    print('[Cloud Python] Signature: VALID')
    print('[Cloud Python] Authentication tag: VALID; decryption: SUCCESS')
    for label, value in result.items():
        print(f'[Cloud Python] {label}: {value}')
    print('[Cloud Python] PASS — ECU and Cloud agree on packet format')


if __name__ == '__main__':
    main()
