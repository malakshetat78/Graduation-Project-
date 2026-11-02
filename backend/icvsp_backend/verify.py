"""Checks on one signed device report, in the order a gateway would apply them (cheapest and least
trusting first). The device signs the exact event bytes, so the event is cut out of the received line,
never re-serialised (signer.h: "ICVSP-v1" 0x00 sender 0x00 msg_id 0x00 utc_ms(8, big-endian) payload)."""
from __future__ import annotations

import json
import struct
from dataclasses import dataclass

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey

PREFIX = '{"event":'
SUFFIX = ',"sender":'
MAX_LINE = 2048
MAX_DEVICE_ID = 64
MAX_EVENT_ID = 128
MAX_INT64 = 2**63 - 1


class Rejected(Exception):
    """A final answer about this report: the phone must not retry it (a retry would get the same answer)."""

    def __init__(self, status: int, reason: str, code: str = "rejected"):
        super().__init__(reason)
        self.status, self.reason, self.code = status, reason, code


@dataclass
class Envelope:
    line: str             # the received line without surrounding whitespace
    event: dict
    payload: bytes        # the signed event bytes
    sender: str
    msg_id: str
    utc_ms: int
    signature: bytes


def signed_bytes(sender: str, msg_id: str, utc_ms: int, payload: bytes) -> bytes:
    return b"ICVSP-v1\0" + sender.encode() + b"\0" + msg_id.encode() + b"\0" + struct.pack(">q", utc_ms) + payload


def parse(raw: bytes) -> Envelope:
    if len(raw) > MAX_LINE:
        raise Rejected(413, "report too large", "too_large")
    try:
        line = raw.decode().strip()   # line endings and spaces around the line are not part of the signed bytes
        if "\\u0000" in line:         # PostgreSQL text cannot hold NUL, and no real report contains one
            raise ValueError
        d = json.loads(line)
        start, end = len(PREFIX), line.index(SUFFIX)
        if not line.startswith(PREFIX):
            raise ValueError
        payload = line[start:end].encode()
        if json.loads(payload) != d["event"]:
            raise ValueError
        sig = bytes.fromhex(d["signature"])
        sender, msg_id, utc_ms = d["sender"], d["msg_id"], d["utc_ms"]
        if len(sig) != 64 or not isinstance(utc_ms, int) or isinstance(utc_ms, bool) or abs(utc_ms) > MAX_INT64:
            raise ValueError
        if not (isinstance(sender, str) and isinstance(msg_id, str)
                and 0 < len(sender) <= MAX_DEVICE_ID and 0 < len(msg_id) <= MAX_EVENT_ID):
            raise ValueError
        sender.encode(), msg_id.encode()   # lone surrogates from \\ud800 escapes raise here
        return Envelope(line, d["event"], payload, sender, msg_id, utc_ms, sig)
    except (ValueError, KeyError, TypeError, RecursionError):
        raise Rejected(400, "not a signed ICVSP report (forward the device's line unchanged)", "malformed") from None


def verify_signature(env: Envelope, public_key_hex: str) -> None:
    key = Ed25519PublicKey.from_public_bytes(bytes.fromhex(public_key_hex))
    try:
        key.verify(env.signature, signed_bytes(env.sender, env.msg_id, env.utc_ms, env.payload))
    except InvalidSignature:
        raise Rejected(401, "signature does not verify with the registered key", "bad_signature") from None
