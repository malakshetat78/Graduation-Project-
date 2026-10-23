#!/usr/bin/env python3
"""Check one report line emitted by the ESP-IDF firmware against the
real ICVSP backend's unchanged signed-byte format.

Usage (from repo root):
    python tools/check_backend_contract.py report.json

Only validates the cryptographic envelope; acceptance by /reports also
requires device registration, current timestamps, schema and replay checks.
"""
import json
import struct
import sys
from pathlib import Path
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

def check(raw: bytes) -> None:
    raw = raw.strip()
    if len(raw) > 2048: raise ValueError("oversized line")
    line = raw.decode("utf-8")
    if not line.startswith('{"event":'): raise ValueError("event must be first")
    payload = line[len('{"event":'):line.index(',"sender":')].encode()
    env = json.loads(raw)
    if json.loads(payload) != env["event"]:
        raise ValueError("different event contents")
    if env["event"].get("schema_version") != 2:
        raise ValueError("expected schema_version=2")
    if env["sender"] != env["event"]["device_id"] or env["msg_id"] != env["event"]["event_id"]:
        raise ValueError("header does not match event")
    signed = b"ICVSP-v1\0" + env["sender"].encode() + b"\0" + env["msg_id"].encode() + b"\0" + struct.pack(">q", env["utc_ms"]) + payload
    Ed25519PublicKey.from_public_bytes(bytes.fromhex(env["public_key"])).verify(bytes.fromhex(env["signature"]), signed)
    print("PASS: Ed25519 signature matches ICVSP backend signed bytes and schema v2 envelope")

if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: python tools/check_backend_contract.py report.json")
    check(Path(sys.argv[1]).read_bytes())
