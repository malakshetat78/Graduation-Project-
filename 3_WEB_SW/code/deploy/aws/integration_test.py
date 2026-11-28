"""T-004 integration and security checks against the deployed backend (AWS Lambda function URL).

Part A (read-only, writes nothing):   health, ready, and refused calls without credentials.
Part B (writes a few test rows):      one test device, a handful of signed reports, the 1-minute evaluator,
                                      then the device is revoked, also when a check fails.

Part B is a dry run unless --execute is given: it prints every request it would send and the rows it would
leave behind. Reports are signed with the team's device format (backend/tests/test_api.py `Car`) using a key
generated in memory for this run only; no real ECU key is used and no private key is ever written anywhere.
The admin token is read from SSM Parameter Store at run time, kept in memory, and never printed or saved.

    python deploy/aws/integration_test.py --url https://<id>.lambda-url.us-east-1.on.aws --part A
    python deploy/aws/integration_test.py --url ... --part B              # dry run: shows the plan
    AWS_PROFILE=icvsp python deploy/aws/integration_test.py --url ... --part B --execute
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "backend"))
import icvsp_backend  # noqa: F401,E402  (puts the trust engine on the import path)
from tests.test_api import Car  # noqa: E402  (the team's signer: same signed bytes as the firmware)

DEVICE = "t004-test-01"
UNREGISTERED = "t004-test-unregistered"
LAT, LON = 23.5, 27.0          # Western Desert, far from any route the team drives
STALE_S = 3 * 24 * 3600        # older than the 48 h pothole window
EVAL_TIMEOUT_S = 180           # the evaluator runs once a minute on Lambda


def mask(text: str) -> str:
    """Remove anything secret-looking before a response is printed or saved."""
    return re.sub(r'("pairing_code"\s*:\s*")[^"]*"', r'\1****"', text)


class Api:
    def __init__(self, base: str, admin_token: str | None = None):
        self.base, self.admin_token, self.log = base.rstrip("/"), admin_token, []

    def call(self, name: str, method: str, path: str, body: bytes | dict | None = None, admin: bool = False,
             expect: tuple[int, ...] = (200,), writes: str = "nothing") -> dict:
        data = json.dumps(body).encode() if isinstance(body, dict) else body
        req = urllib.request.Request(self.base + path, data=data, method=method,
                                     headers={"Content-Type": "application/json", "User-Agent": "icvsp-t004-test"})
        if admin and self.admin_token:
            req.add_header("X-Admin-Token", self.admin_token)
        for attempt in range(3):  # a flaky laptop network is retried; an HTTP answer never is
            t0 = time.perf_counter()
            try:
                with urllib.request.urlopen(req, timeout=30) as r:
                    status, raw = r.status, r.read().decode()
                break
            except urllib.error.HTTPError as e:
                status, raw = e.code, e.read().decode()
                break
            except urllib.error.URLError as e:
                status, raw = None, f"network error: {e.reason}"
                time.sleep(2 * (attempt + 1))
        ms = round((time.perf_counter() - t0) * 1000)
        entry = {"check": name, "request": f"{method} {path}", "expected": list(expect), "status": status,
                 "passed": status in expect, "ms": ms, "writes": writes, "response": mask(raw)[:400]}
        self.log.append(entry)
        print(f"{'PASS' if entry['passed'] else 'FAIL'}  {name:<42} {status}  {ms:>5} ms  {entry['response'][:90]}")
        return entry


def admin_token_from_ssm(name: str) -> str:
    out = subprocess.run(["aws", "ssm", "get-parameter", "--name", name, "--with-decryption",
                          "--query", "Parameter.Value", "--output", "text"], capture_output=True, text=True)
    if out.returncode != 0 or not out.stdout.strip():
        raise SystemExit(f"could not read {name} from SSM (is AWS_PROFILE set?): {out.stderr.strip()[:200]}")
    return out.stdout.strip()


def part_a(api: Api) -> None:
    api.call("A1 health", "GET", "/health")
    api.call("A2 ready (database reachable)", "GET", "/ready")
    api.call("A3 register device without admin token", "POST", "/devices",
             {"device_id": "t004-never-stored", "public_key": "00" * 32}, expect=(401, 403))
    api.call("A4 operator overview without admin token", "GET", "/admin/overview", expect=(401, 403))
    api.call("A5 hazard search without sign-in", "GET", f"/hazards?lat={LAT}&lon={LON}", expect=(401, 403))
    api.call("A6 report status without sign-in", "GET", f"/reports/{DEVICE}/none", expect=(401, 403))


def plan_b(car: Car, ghost: Car) -> list[tuple]:
    now = time.time()
    valid = car.report(now - 2, LAT, LON).decode()
    stale = car.report(now - STALE_S, LAT, LON).decode()
    unsent = car.report(now - 2, LAT, LON + 0.001).decode()
    bad_sig = re.sub(r'"signature":"([0-9a-f]{127})([0-9a-f])"',
                     lambda m: f'"signature":"{m.group(1)}{"0" if m.group(2) != "0" else "1"}"', unsent)
    tampered = car.report(now - 2, LAT, LON + 0.002).decode().replace('"severity":"high"', '"severity":"low"')
    stranger = ghost.report(now - 2, LAT, LON).decode()
    reg = {"device_id": DEVICE, "public_key": car.public_hex, "vetted": False}
    return [
        ("B1 register test device", "POST", "/devices", reg, True, (201,), "1 row in devices"),
        ("B2 valid signed report", "POST", "/reports", valid.encode(), False, (201,), "1 row in reports"),
        ("B3 same report again (duplicate)", "POST", "/reports", valid.encode(), False, (201,), "nothing new"),
        ("B4 stale report (3 days old)", "POST", "/reports", stale.encode(), False, (422,), "1 row in rejections"),
        ("B5 invalid signature", "POST", "/reports", bad_sig.encode(), False, (401,), "1 row in rejections"),
        ("B6 payload changed after signing", "POST", "/reports", tampered.encode(), False, (401,), "1 row in rejections"),
        ("B7 unregistered device", "POST", "/reports", stranger.encode(), False, (403,), "1 row in rejections"),
    ]


def overview(api: Api) -> dict:
    """The operator overview (read only). Callers keep only the test rows; other team data is never saved."""
    req = urllib.request.Request(api.base + "/admin/overview",
                                 headers={"X-Admin-Token": api.admin_token, "User-Agent": "icvsp-t004-test"})
    with urllib.request.urlopen(req, timeout=30) as r:
        return json.loads(r.read().decode())


def wait_for_evaluator(api: Api, event_id: str) -> dict:
    t0, seen = time.time(), {}
    while time.time() - t0 < EVAL_TIMEOUT_S:
        ov = overview(api)
        mine = [r for r in ov.get("reports", []) if r["event_id"] == event_id]
        if mine and mine[0]["evaluation"] != "pending":
            seen = {"report": mine[0],
                    "hazards": [h for h in ov["hazards"] if DEVICE in h.get("support", [])],
                    "device": [d for d in ov["devices"] if d["device_id"] == DEVICE],
                    "seconds_until_evaluated": round(time.time() - t0)}
            break
        time.sleep(10)
    entry = {"check": "B8 trust engine evaluated the report (1-minute schedule)", "request": "GET /admin/overview",
             "expected": ["evaluated within 180 s"], "status": 200 if seen else None, "passed": bool(seen),
             "ms": None, "writes": "the evaluator may add 1 test hazard + evidence",
             "response": json.dumps(seen)[:1500]}
    api.log.append(entry)
    print(f"{'PASS' if seen else 'FAIL'}  {entry['check']}  {seen.get('seconds_until_evaluated', 'timeout')} s")
    for h in seen.get("hazards", []):
        level = {"unconfirmed": "advisory (no alert)", "corroborated": "WARNING", "confirmed": "WARNING"}.get(
            h["state"], "not shown to drivers")
        print(f"      hazard {h['hazard_id']} state={h['state']} level={level} sources={h['independent_sources']}")
    return seen


def after_revoke(api: Api, car: Car) -> None:
    """B11: a report in the revoked device's name is refused. The backend checks the device status before the
    signature, so this also works with a new key when the run's key is gone (the device is already revoked)."""
    line = car.report(time.time() - 2, LAT, LON).decode().encode()
    api.call("B11 report after revoke is refused", "POST", "/reports", line, expect=(403,),
             writes="1 row in rejections")


def save(api: Api, part: str, out_dir: Path, extra: dict) -> Path:
    out_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    path = out_dir / f"t004-part{part}-{stamp}.json"
    path.write_text(json.dumps({"part": part, "target": api.base, "utc": stamp, **extra, "checks": api.log},
                               indent=2), encoding="utf-8")
    return path


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", required=True)
    ap.add_argument("--part", choices=("A", "B", "B11"), required=True,
                    help="B11 alone: one report in the name of the already revoked test device")
    ap.add_argument("--execute", action="store_true", help="Part B only: really send the writes")
    ap.add_argument("--admin-token-ssm", default="/icvsp/admin_token")
    ap.add_argument("--out", default=str(ROOT / "deploy/aws/results"))
    a = ap.parse_args()

    if a.part == "A":
        api = Api(a.url)
        part_a(api)
        print("saved", save(api, "A", Path(a.out), {"writes": "none"}))
        sys.exit(0 if all(c["passed"] for c in api.log) else 1)

    if a.part == "B11":
        car = Car(DEVICE, boot=2)  # new in-memory key; the run's key is gone and the device is revoked anyway
        if not a.execute:
            print(f"DRY RUN: would POST one report as {DEVICE} (revoked), expect 403 device_revoked, "
                  "writes 1 row in rejections. Run again with --execute to send.")
            return
        api = Api(a.url)
        after_revoke(api, car)
        print("saved", save(api, "B11", Path(a.out), {"test_device": DEVICE, "location": [LAT, LON]}))
        sys.exit(0 if all(c["passed"] for c in api.log) else 1)

    car, ghost = Car(DEVICE), Car(UNREGISTERED)   # fresh keys, in memory only
    steps = plan_b(car, ghost)
    if not a.execute:
        print(f"DRY RUN against {a.url} (nothing sent). Test device {DEVICE}, location {LAT}, {LON}.")
        for name, method, path, _, admin, expect, writes in steps:
            print(f"  {name:<36} {method} {path:<9} expect {expect}  writes: {writes}")
        print(f"  {'B8 wait for the evaluator':<36} GET /admin/overview (read only, up to {EVAL_TIMEOUT_S} s)")
        print(f"  {'B9 revoke test device':<36} POST /devices/{DEVICE}/revoke   changes: our test device only")
        print("Run again with --execute to send.")
        return

    local = a.url.startswith(("http://127.0.0.1", "http://localhost"))
    token = os.environ.get("ICVSP_LOCAL_ADMIN_TOKEN") if local else None  # rehearsal on a laptop server only
    api = Api(a.url, token or admin_token_from_ssm(a.admin_token_ssm))
    registered, evaluation = False, {}
    try:
        for name, method, path, body, admin, expect, writes in steps:
            e = api.call(name, method, path, body, admin=admin, expect=expect, writes=writes)
            if name.startswith("B1"):
                registered = e["status"] == 201
                if not registered:
                    raise SystemExit("registration failed; stopping before any report is sent")
        valid_event = f"{DEVICE}-1-1"
        evaluation = wait_for_evaluator(api, valid_event)
    finally:
        if registered:
            api.call("B9 revoke test device", "POST", f"/devices/{DEVICE}/revoke", admin=True,
                     writes="test device status set to REVOKED")
            status = [d["status"] for d in overview(api)["devices"] if d["device_id"] == DEVICE]
            api.log.append({"check": "B10 test device shows REVOKED (read only)", "request": "GET /admin/overview",
                            "expected": ["REVOKED"], "status": 200, "passed": status == ["REVOKED"], "ms": None,
                            "writes": "nothing", "response": json.dumps(status)})
            print(f"{'PASS' if status == ['REVOKED'] else 'FAIL'}  B10 test device shows REVOKED   {status}")
            after_revoke(api, car)
        print("saved", save(api, "B", Path(a.out), {"test_device": DEVICE, "public_key": car.public_hex,
                                                     "location": [LAT, LON], "evaluation": evaluation}))
    sys.exit(0 if all(c["passed"] for c in api.log) else 1)


if __name__ == "__main__":
    main()
