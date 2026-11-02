"""End-to-end check against a local Supabase stack: real sign-up and e-mail confirmation, real tokens, the server killed
right after an ACK, one-time alerts, and direct attacks on Supabase's public API. Needs Docker. Local stacks only.

Run:
    (cd database && supabase start)
    pip install -r backend/requirements.txt
    python verification/e2e_supabase.py
    (cd database && supabase stop --no-backup)
"""
import json, os, re, signal, subprocess, sys, time, secrets
from pathlib import Path
from urllib.parse import urlparse

import httpx, psycopg

R = str(Path(__file__).resolve().parents[1])
sys.path.insert(0, R + "/backend")
import icvsp_backend  # noqa: F401,E402  (puts the engine on the import path)
from icvsp.geo import offset  # noqa: E402
from tests.test_api import Car  # noqa: E402

status = subprocess.run(["supabase", "status", "-o", "json"], cwd=R + "/database", capture_output=True, text=True, check=True)
cfg = json.loads(status.stdout[status.stdout.index("{"):])
db_url = urlparse(cfg["DB_URL"])
if db_url.hostname not in ("127.0.0.1", "localhost") or urlparse(cfg["API_URL"]).hostname not in ("127.0.0.1", "localhost"):
    raise SystemExit("refusing to run: this changes a database role and is for a local stack only")
ANON = cfg.get("ANON_KEY") or cfg["PUBLISHABLE_KEY"]
AUTH, REST = cfg["API_URL"] + "/auth/v1", cfg["REST_URL"]
pw, admin_token = secrets.token_hex(16), secrets.token_hex(16)
with psycopg.connect(cfg["DB_URL"], autocommit=True) as db:
    db.execute(f"alter role icvsp_backend login password '{pw}'")
API = "http://127.0.0.1:8123"
env = {**os.environ, "DATABASE_URL": f"postgresql://icvsp_backend:{pw}@127.0.0.1:{db_url.port}/postgres",
       "SUPABASE_URL": cfg["API_URL"], "ICVSP_ADMIN_TOKEN": admin_token, "ICVSP_WORKER_INTERVAL_S": "3", "PYTHONPATH": R + "/backend"}
def start():
    p = subprocess.Popen([sys.executable, "-m", "uvicorn", "--factory", "icvsp_backend.app:create_app", "--port", "8123", "--log-level", "warning"], cwd=R + "/backend", env=env)
    for _ in range(50):
        try:
            if httpx.get(API + "/ready").status_code == 200: return p
        except httpx.TransportError: pass
        time.sleep(0.2)
    raise SystemExit("server did not start")
def ok(name, cond, extra=""):
    print(("PASS " if cond else "FAIL ") + name, extra)
    if not cond: ok.failed += 1
ok.failed = 0
h_anon = {"apikey": ANON, "Authorization": f"Bearer {ANON}"}

def sign_up(email):
    password = secrets.token_urlsafe(16)
    r = httpx.post(AUTH + "/signup", headers={"apikey": ANON}, json={"email": email, "password": password})
    assert r.status_code == 200, r.text
    return password
def login(email, password):
    return httpx.post(AUTH + "/token?grant_type=password", headers={"apikey": ANON}, json={"email": email, "password": password})
def confirm(email):
    mail = cfg.get("INBUCKET_URL") or cfg.get("MAILPIT_URL") or "http://127.0.0.1:54324"
    for _ in range(30):
        msgs = httpx.get(mail + "/api/v1/messages").json()["messages"]
        m = [x for x in msgs if x["To"][0]["Address"] == email]
        if m: break
        time.sleep(0.5)
    body = httpx.get(f"{mail}/api/v1/message/{m[0]['ID']}").json()["Text"]
    link = re.search(r"(http\S+verify\S+)", body).group(1)
    httpx.get(link, follow_redirects=False)

server = start()
try:
    alice_mail, bob_mail = f"alice{secrets.token_hex(3)}@test.invalid".replace("test.invalid","example.com"), f"bob{secrets.token_hex(3)}@example.com"
    apw, bpw = sign_up(alice_mail), sign_up(bob_mail)
    ok("unconfirmed e-mail cannot sign in", login(alice_mail, apw).status_code != 200)
    confirm(alice_mail); confirm(bob_mail)
    tok_a, tok_b = login(alice_mail, apw), login(bob_mail, bpw)
    ok("confirmed user signs in", tok_a.status_code == 200, tok_a.text[:100] if tok_a.status_code != 200 else "")
    A = {"Authorization": "Bearer " + tok_a.json()["access_token"]}; B = {"Authorization": "Bearer " + tok_b.json()["access_token"]}
    print("     token alg:", __import__("jwt").get_unverified_header(tok_a.json()["access_token"])["alg"])
    me = httpx.get(API + "/me", headers=A)
    ok("real Supabase token accepted, trigger made a driver profile", me.status_code == 200 and me.json()["role"] == "driver", me.text[:120])
    ok("no token refused", httpx.get(API + "/me").status_code == 401)
    ok("forged token refused", httpx.get(API + "/me", headers={"Authorization": "Bearer " + tok_a.json()["access_token"][:-4] + "AAAA"}).status_code == 401)

    ADM = {"X-Admin-Token": admin_token}
    cars = [Car(f"e2e-{secrets.token_hex(2)}-{i}") for i in range(2)]
    for c in cars:
        reg = httpx.post(API + "/devices", headers=ADM, json={"device_id": c.id, "public_key": c.public_hex, "vetted": True})
        claim = httpx.post(API + "/me/devices", headers=A, json={"device_id": c.id, "pairing_code": reg.json()["pairing_code"]})
        assert claim.status_code == 201, claim.text
    ok("device linked", set(httpx.get(API + "/me", headers=A).json()["devices"]) == {c.id for c in cars})

    LAT, LON = 30.0444, 31.2357 + secrets.randbelow(1000) / 10000
    now = time.time()
    for i, c in enumerate(cars):
        t = now - 40 + 15 * i
        rows = [{"vehicle_id": c.id, "t": t - 10 + k, "lat": offset(LAT, LON, 0, -110 + 11 * k)[0], "lon": offset(LAT, LON, 0, -110 + 11 * k)[1], "speed_mps": 11, "heading_deg": 90} for k in range(25)]
        r = httpx.post(API + "/beacons", headers=A, json=rows); assert r.status_code == 200, r.text
    ok("another user's positions refused", httpx.post(API + "/beacons", headers=B, json=rows).status_code == 403)

    lines = [c.report(now - 40 + 15 * i, LAT, LON) for i, c in enumerate(cars)]
    acks = [httpx.post(API + "/reports", content=l) for l in lines]
    ok("reports acked as stored, evaluation pending", all(a.status_code == 201 and a.json()["evaluation"] == "pending" for a in acks), acks[0].text[:120])
    os.kill(server.pid, signal.SIGKILL); server.wait()
    print("     server killed with SIGKILL right after the ACK")
    with psycopg.connect(cfg["DB_URL"]) as db:
        n = db.execute("select count(*) from public.reports where evaluation_status='pending' and device_id = any(%s)", ([c.id for c in cars],)).fetchone()[0]
    ok("both reports survived, still pending", n == 2, str(n))
    server = start()
    ok("retry after restart gets the earlier ACK", httpx.post(API + "/reports", content=lines[0]).json()["status"] == "duplicate")
    for _ in range(40):
        rr = httpx.get(f"{API}/reports/{cars[0].id}/{cars[0].id}-1-1", headers=A).json()
        if rr["evaluation"] != "pending": break
        time.sleep(0.5)
    ok("worker finished evaluation after restart", rr["evaluation"] == "evaluated" and rr["hazard_ids"], str(rr["evaluation"]))
    ok("other user cannot read the report", httpx.get(f"{API}/reports/{cars[0].id}/{cars[0].id}-1-1", headers=B).status_code == 403)

    plat, plon = offset(LAT, LON, 0, -450)
    first = httpx.post(API + "/hazards/ahead", headers=B, json={"lat": plat, "lon": plon, "heading_deg": 90}).json()
    second = httpx.post(API + "/hazards/ahead", headers=B, json={"lat": plat, "lon": plon, "heading_deg": 90}).json()
    ok("hazard 450 m ahead alerts once", first["hazards"] and first["hazards"][0]["alert"] and not second["hazards"][0]["alert"], json.dumps(first)[:160])
    print("     state:", first["hazards"][0]["state"], "distance:", first["hazards"][0]["distance_m"])
    hz = first["hazards"][0]["hazard_id"]

    # straight at Supabase's public API, the way someone with the app's public key could
    R_a = {"apikey": ANON, "Authorization": A["Authorization"]}; R_b = {"apikey": ANON, "Authorization": B["Authorization"]}
    mine = httpx.get(REST + "/reports?select=device_id", headers=R_a).json()
    ok("PostgREST: owner reads own reports", len(mine) == 2, str(mine)[:100])
    ok("PostgREST: other user reads none", httpx.get(REST + "/reports?select=device_id", headers=R_b).json() == [])
    ok("PostgREST: anon key gets refused", httpx.get(REST + "/hazards?select=*", headers=h_anon).status_code in (401, 403))
    ok("PostgREST: driver reads published hazard", len(httpx.get(f"{REST}/hazards?select=hazard_id&hazard_id=eq.{hz}", headers=R_b).json()) == 1)
    ok("PostgREST: rpc nearby_hazards works for a user", len(httpx.post(REST + "/rpc/nearby_hazards", headers=R_b, json={"p_lat": LAT, "p_lon": LON, "p_radius_m": 100, "p_states": ["corroborated", "confirmed"]}).json()) >= 1)
    ok("PostgREST: anon cannot call rpc", httpx.post(REST + "/rpc/nearby_hazards", headers=h_anon, json={"p_lat": LAT, "p_lon": LON, "p_radius_m": 100}).status_code in (401, 403))
    for name, resp in (("confirm a hazard", httpx.patch(f"{REST}/hazards?hazard_id=eq.{hz}", headers=R_b, json={"state": "suppressed"})),
                       ("raise reputation", httpx.patch(f"{REST}/devices?device_id=eq.{cars[0].id}", headers=R_a, json={"good": 999})),
                       ("become admin", httpx.patch(f"{REST}/profiles?user_id=eq.{me.json()['user_id']}", headers=R_a, json={"role": "admin"})),
                       ("insert evidence", httpx.post(f"{REST}/hazard_evidence", headers=R_a, json={"hazard_id": hz, "device_id": "x", "event_id": "y", "c": 1, "g": 1, "h": 1, "f": 1, "weight": 1})),
                       ("read pairing hash", httpx.get(f"{REST}/devices?select=pairing_code_hash", headers=R_a))):
        ok(f"PostgREST: user cannot {name}", resp.status_code in (401, 403), str(resp.status_code))
finally:
    server.terminate(); server.wait()
    with psycopg.connect(cfg["DB_URL"], autocommit=True) as db:
        db.execute("alter role icvsp_backend nologin password null")
print("FAILED:", ok.failed)
sys.exit(1 if ok.failed else 0)
