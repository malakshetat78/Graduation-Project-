"""Shared pytest fixtures for the ROVO standalone simulation."""
from pathlib import Path
import secrets

import pytest
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
from fastapi.testclient import TestClient

from cloud_api import create_app


@pytest.fixture
def setup(tmp_path: Path):
    key_dir = tmp_path / 'keys'
    key_dir.mkdir()
    seed = secrets.token_bytes(32)
    signing_key = Ed25519PrivateKey.from_private_bytes(seed)
    public = signing_key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw)
    (key_dir / 'sign_seed.bin').write_bytes(seed)
    (key_dir / 'sign_pub.bin').write_bytes(public)
    db = tmp_path / 'cloud.sqlite3'
    mobile_db = tmp_path / 'mobile.sqlite3'
    with TestClient(create_app(db_path=db, keys_dir=key_dir)) as client:
        yield {
            'tmp_path': tmp_path,
            'key_dir': key_dir,
            'signing_key': signing_key,
            'client': client,
            'db': db,
            'mobile_db': mobile_db,
        }
