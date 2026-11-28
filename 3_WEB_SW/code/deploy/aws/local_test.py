"""Calls the Lambda package running in the local Lambda container (see README.md, "Test without AWS") with
function-URL events: health, ready, register, signed report, duplicate, tampered, no admin token, evaluator."""
import json, sys, time, urllib.request
import struct
from datetime import datetime, timezone
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat
def to_iso(t): return datetime.fromtimestamp(t, timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")
class Car:  # same signed layout as backend/tests/test_api.py Car
    def __init__(self, device_id, boot=1):
        self.id, self.boot, self.seq = device_id, boot, 0
        self.key = Ed25519PrivateKey.generate()
        self.public_hex = self.key.public_key().public_bytes(Encoding.Raw, PublicFormat.Raw).hex()
    def report(self, t, lat, lon):
        self.seq += 1
        ev = {"schema_version": 2, "event_id": f"{self.id}-{self.boot}-{self.seq}", "device_id": self.id,
              "boot_id": self.boot, "sequence": self.seq, "type": "pothole", "detected_at": to_iso(t),
              "gps_valid": True, "latitude": lat, "longitude": lon, "position_accuracy_m": 3.5,
              "speed_kmh": 40.0, "heading_deg": 90.0, "severity": "high", "detection_confidence": 0.9, "model": "imu-v0"}
        payload = json.dumps(ev, separators=(",", ":")); utc_ms = int(t * 1000)
        msg = b"ICVSP-v1\0" + self.id.encode() + b"\0" + ev["event_id"].encode() + b"\0" + struct.pack(">q", utc_ms)
        sig = self.key.sign(msg + payload.encode()).hex()
        return (f'{{"event":{payload},"sender":"{self.id}","msg_id":"{ev["event_id"]}","utc_ms":{utc_ms},'
                f'"public_key":"{self.public_hex}","signature":"{sig}"}}').encode()
INV = "http://127.0.0.1:9011/2015-03-31/functions/function/invocations"

def call(method, path, body=None, admin=False, query=""):
    h = {"content-type": "application/json", "host": "x.lambda-url.us-east-1.on.aws"}
    if admin: h["x-admin-token"] = "localtest"
    ev = {"version": "2.0", "routeKey": "$default", "rawPath": path, "rawQueryString": query, "headers": h,
          "requestContext": {"http": {"method": method, "path": path, "sourceIp": "1.2.3.4", "protocol": "HTTP/1.1",
                             "userAgent": "t"}, "domainName": "x.lambda-url.us-east-1.on.aws", "stage": "$default",
                             "requestId": "r", "accountId": "anonymous", "apiId": "x", "time": "", "timeEpoch": 0},
          "body": body if isinstance(body, str) or body is None else json.dumps(body), "isBase64Encoded": False}
    r = json.loads(urllib.request.urlopen(INV, json.dumps(ev).encode()).read())
    return r.get("statusCode"), r.get("body", r)

car = Car("aws-local-test-01")
print("health", *call("GET", "/health"))
print("ready", *call("GET", "/ready"))
print("register", call("POST", "/devices", {"device_id": car.id, "public_key": car.public_hex, "vetted": False}, admin=True)[0])
line = car.report(time.time() - 2, 30.0444, 31.2357).decode()
print("report", *call("POST", "/reports", line))
print("again", *call("POST", "/reports", line))
print("tampered", *call("POST", "/reports", line.replace('"severity":"high"', '"severity":"low"')))
print("no admin token", call("POST", "/devices", {"device_id": "x", "public_key": car.public_hex})[0])
r = json.loads(urllib.request.urlopen(INV, b'{"icvsp_task":"evaluate"}').read()); print("evaluate", r)
print("ready after", *call("GET", "/ready"))
