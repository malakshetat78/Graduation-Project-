"""Generate demo-only shared XChaCha20 key + Ed25519 seed/public key.
Run once, before the C++ simulator. Never commit these keys.
"""
from pathlib import Path
import os
import secrets
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

KEY_DIR = Path('keys')
FILES = ('enc_key.bin', 'sign_seed.bin', 'sign_pub.bin')


def write_secret(path: Path, value: bytes):
    fd = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
    with os.fdopen(fd, 'wb') as f:
        f.write(value)


def main():
    KEY_DIR.mkdir(mode=0o700, exist_ok=True)
    if any((KEY_DIR / f).exists() for f in FILES):
        raise SystemExit('Keys already exist. Reuse them for testing; do not overwrite accidentally.')
    secret = secrets.token_bytes(32)
    seed = secrets.token_bytes(32)
    public = Ed25519PrivateKey.from_private_bytes(seed).public_key().public_bytes(
        Encoding.Raw, PublicFormat.Raw
    )
    write_secret(KEY_DIR / 'enc_key.bin', secret)
    write_secret(KEY_DIR / 'sign_seed.bin', seed)
    write_secret(KEY_DIR / 'sign_pub.bin', public)
    print('[Key setup] Created 32-byte symmetric key and Ed25519 key pair in keys/')
    print('[Key setup] DEMO ONLY — not suitable as device provisioning infrastructure')


if __name__ == '__main__':
    main()
