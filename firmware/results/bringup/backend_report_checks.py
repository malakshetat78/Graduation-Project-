"""Backend checks on the report the emulator sent (HOST). Run after the report demo, with the demo backend still up.

usage: python3 backend_report_checks.py <demo sqlite file> [backend URL, default http://127.0.0.1:8010]

1. GET /reports for the device that sent the latest report: what was stored (no position printed).
2. POST the same signed line again: must be accepted as a duplicate (a safe retry after a lost answer).
3. POST it with one field changed (severity high -> low), signature kept: must be refused (bad signature).
Only the standard library is used. The signed line itself is not printed (it holds the demo coordinates).
"""
import json
import sqlite3
import sys
import urllib.error
import urllib.parse
import urllib.request

db = sys.argv[1]
base = sys.argv[2] if len(sys.argv) > 2 else "http://127.0.0.1:8010"


def post(body):
    req = urllib.request.Request(base + "/reports", data=body, method="POST",
                                 headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req) as r:
            return r.status, r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode()


con = sqlite3.connect(db)
device, line = con.execute("select device_id, line from reports order by received_at desc").fetchone()
line = line.encode()
print(f"# HOST checks against the laptop backend ({base}, local SQLite), after the emulator run")
print(f"$ GET /reports?device_id={device}")
with urllib.request.urlopen(base + "/reports?device_id=" + urllib.parse.quote(device)) as r:
    for x in json.load(r):
        print(json.dumps({k: x[k] for k in ("event_id", "device_id", "type", "severity", "detection_confidence",
                                            "evaluation")}))
print("$ POST /reports  (the same signed line again, as a retry after a lost answer)")
print(*post(line))
changed = line.replace(b'"severity":"high"', b'"severity":"low"', 1)
if changed == line:
    sys.exit("report is not severity high; pick another field to change")
print("$ POST /reports  (same line, one field changed: severity high -> low, signature kept)")
print(*post(changed))
