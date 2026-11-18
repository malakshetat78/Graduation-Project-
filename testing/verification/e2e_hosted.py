"""End-to-end check against an already migrated Supabase project, through the real backend running on this machine.
It only uses the restricted role icvsp_backend and the Auth admin API: no schema changes, no migrations. It refuses to run
if the project holds real devices, or any reports or hazards, because the evaluator works on everything it finds.
Test rows are left in place for inspection; they are named e2etest-* so they can be removed afterwards.

The settings file (.env.hosted at the repository root, never committed) holds:
    DATABASE_URL               role icvsp_backend, see database/README.md
    SUPABASE_URL               https://<project-ref>.supabase.co
    SUPABASE_ANON_KEY          the public (anon / publishable) key
    SUPABASE_SERVICE_ROLE_KEY  used only to create the two confirmed test users

Run:
    pip install -r backend/requirements.txt
    python verification/e2e_hosted.py
"""
import os, secrets, signal, subprocess, sys, time
from pathlib import Path

import httpx, psycopg

R = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(R / "backend"))
import icvsp_backend  # noqa: F401,E402  (puts the engine on the import path)
from icvsp.geo import offset  # noqa: E402
from tests.test_api import Car  # noqa: E402

env_file = Path(sys.argv[1]) if len(sys.argv) > 1 else R / ".env.hosted"
if not env_file.exists():
    raise SystemExit(f"missing settings file: {env_file}")
cfg = dict(l.split("=", 1) for l in env_file.read_text().splitlines() if "=" in l and not l.lstrip().startswith("#"))
cfg = {k.strip(): v.strip().strip("'\"") for k, v in cfg.items()}
missing = {"DATABASE_URL", "SUPABASE_URL", "SUPABASE_ANON_KEY", "SUPABASE_SERVICE_ROLE_KEY"} - {k for k, v in cfg.items() if v}
if missing:
    raise SystemExit(f"missing settings: {sorted(missing)}")
DB, ANON, SERVICE = cfg["DATABASE_URL"], cfg["SUPABASE_ANON_KEY"], cfg["SUPABASE_SERVICE_ROLE_KEY"]
BASE = cfg["SUPABASE_URL"].rstrip("/")
AUTH, REST = BASE + "/auth/v1", BASE + "/rest/v1"
LEGACY_SCHEMAS = ("icvsp_legacy_20261103", "icvsp_backup_20261103")
API = "http://127.0.0.1:8124"
RUN = secrets.token_hex(3)
http = httpx.Client(timeout=30)


def ok(name, cond, extra=""):
    print(("PASS " if cond else "FAIL ") + name, extra, flush=True)
    if not cond:
        ok.failed += 1
ok.failed = 0


def sql(query, params=None):
    with psycopg.connect(DB, autocommit=True, prepare_threshold=None) as db:
        return db.execute(query, params).fetchall()


who = sql("select current_user")[0][0]
if who != "icvsp_backend":
    raise SystemExit(f"refusing to run: connected as {who}, this test must use icvsp_backend")
counts = sql("select (select count(*) from public.devices where device_id not like 'e2etest-%%'), (select count(*) from public.reports), (select count(*) from public.hazards)")[0]
if any(counts):
    raise SystemExit(f"refusing to run: real devices, reports, hazards are not all zero {counts}; remove the earlier test rows first")
for schema in LEGACY_SCHEMAS:
    ok(f"backend role cannot reach {schema}", not sql("select coalesce(bool_or(has_schema_privilege(current_user, oid, 'USAGE')), false) from pg_namespace where nspname = %s", (schema,))[0][0])

admin_token = secrets.token_hex(16)
env = {**os.environ, "DATABASE_URL": DB, "SUPABASE_URL": BASE, "ICVSP_ADMIN_TOKEN": admin_token,
       "ICVSP_WORKER_INTERVAL_S": "3", "PYTHONPATH": str(R / "backend")}
ADM = {"X-Admin-Token": admin_token}


def start():
    p = subprocess.Popen([sys.executable, "-m", "uvicorn", "--factory", "icvsp_backend.app:create_app", "--port", "8124", "--log-level", "warning"], cwd=R / "backend", env=env)
    for _ in range(150):
        try:
            if http.get(API + "/ready").status_code == 200:
                return p
        except httpx.TransportError:
            pass
        time.sleep(0.2)
    p.terminate()
    raise SystemExit("server did not become ready: check DATABASE_URL")


def new_user(name):
    """A confirmed test user; the password exists only inside this run and no e-mail is sent."""
    email, password = f"e2etest-{RUN}-{name}@example.com", secrets.token_urlsafe(24)
    r = http.post(AUTH + "/admin/users", headers={"apikey": SERVICE, "Authorization": "Bearer " + SERVICE},
                  json={"email": email, "password": password, "email_confirm": True})
    if r.status_code != 200:
        raise SystemExit(f"could not create test user: {r.status_code}")
    t = http.post(AUTH + "/token?grant_type=password", headers={"apikey": ANON}, json={"email": email, "password": password})
    if t.status_code != 200:
        raise SystemExit(f"could not sign in test user: {t.status_code}")
    return t.json()["access_token"]


server = start()
try:
    tok_a, tok_b = new_user("a"), new_user("b")
    A, B = {"Authorization": "Bearer " + tok_a}, {"Authorization": "Bearer " + tok_b}
    me = http.get(API + "/me", headers=A)
    ok("real token accepted, driver profile made by the trigger", me.status_code == 200 and me.json()["role"] == "driver", str(me.status_code))
    ok("no token refused", http.get(API + "/me").status_code == 401)
    ok("forged token refused", http.get(API + "/me", headers={"Authorization": "Bearer " + tok_a[:-4] + "AAAA"}).status_code == 401)

    cars = [Car(f"e2etest-{RUN}-{i}") for i in range(2)]
    for c in cars:
        reg = http.post(API + "/devices", headers=ADM, json={"device_id": c.id, "public_key": c.public_hex, "vetted": True})
        claim = http.post(API + "/me/devices", headers=A, json={"device_id": c.id, "pairing_code": reg.json()["pairing_code"]})
        assert claim.status_code == 201, claim.status_code
    ok("devices registered and linked", set(http.get(API + "/me", headers=A).json()["devices"]) == {c.id for c in cars})

    # ---------- beacons ----------
    LAT, LON = 30.0444, 31.2357 + secrets.randbelow(1000) / 10000
    now = time.time()
    for i, c in enumerate(cars):
        t = now - 40 + 15 * i
        rows = [{"vehicle_id": c.id, "t": t - 10 + k, "lat": offset(LAT, LON, 0, -110 + 11 * k)[0], "lon": offset(LAT, LON, 0, -110 + 11 * k)[1], "speed_mps": 11, "heading_deg": 90} for k in range(25)]
        r = http.post(API + "/beacons", headers=A, json=rows)
        ok(f"own positions stored ({c.id})", r.status_code == 200 and r.json()["stored"] == 25, str(r.status_code))
    ok("another user's positions refused", http.post(API + "/beacons", headers=B, json=rows).status_code == 403)
    ok("positions without sign-in refused", http.post(API + "/beacons", json=rows).status_code == 401)
    beacons_stored = sql("select count(*) from public.beacons")[0][0]
    ok("positions are in the database", beacons_stored == 50, str(beacons_stored))

    # ---------- signed reports ----------
    lines = [c.report(now - 40 + 15 * i, LAT, LON) for i, c in enumerate(cars)]
    t0 = time.perf_counter()
    acks = [http.post(API + "/reports", content=l) for l in lines]
    print(f"     ACK time over the network: {(time.perf_counter() - t0) / 2 * 1000:.0f} ms each")
    ok("signed reports acked as stored, evaluation pending", all(a.status_code == 201 and a.json()["evaluation"] == "pending" for a in acks), str([a.status_code for a in acks]))
    ok("two devices' reports of one pothole are both stored", sql("select count(distinct device_id) from public.reports")[0][0] == 2)
    second = cars[0].report(now - 20, LAT, LON)
    ok("a second report from one device about the same pothole is stored, not a duplicate", http.post(API + "/reports", content=second).json().get("status") == "accepted")

    tampered = lines[0].replace(b'"severity":"high"', b'"severity":"low"')
    bad = http.post(API + "/reports", content=tampered)
    ok("changed report refused (signature)", bad.status_code == 401 and bad.json().get("retryable") is False, str(bad.status_code))
    stranger = Car(f"e2etest-{RUN}-unregistered")
    r = http.post(API + "/reports", content=stranger.report(now - 30, LAT, LON))
    ok("unregistered device refused", r.status_code in (401, 403), str(r.status_code))
    imposter = Car(cars[0].id)
    imposter.seq = 50
    r = http.post(API + "/reports", content=imposter.report(now - 30, LAT, LON))
    ok("registered ID signed with another key refused", r.status_code == 401, str(r.status_code))

    ok("same line sent again answered duplicate", http.post(API + "/reports", content=lines[0]).json().get("status") == "duplicate")
    cars[0].seq = 0
    r = http.post(API + "/reports", content=cars[0].report(now - 40, LAT, LON, severity="low"))
    ok("same ID with other content refused", r.status_code == 409 and r.json().get("code") == "id_conflict", str(r.status_code))
    cars[0].seq = 0
    r = http.post(API + "/reports", content=cars[0].report(now - 40, LAT, LON, event_id=f"{cars[0].id}-other"))
    ok("same counter under another ID refused", r.status_code == 409 and r.json().get("code") == "counter_conflict", str(r.status_code))
    cars[0].seq = 10
    r = http.post(API + "/reports", content=cars[0].report(now - 3 * 86400, LAT, LON))
    ok("old pothole report refused", r.status_code == 422 and r.json().get("code") == "stale", str(r.status_code))
    r = http.post(API + "/reports", content=cars[0].report(now + 600, LAT, LON))
    ok("report from the future refused", r.status_code == 422 and r.json().get("code") == "future", str(r.status_code))
    stored = sql("select count(*) from public.reports")[0][0]
    ok("only the 3 good reports were stored", stored == 3, str(stored))
    logged = sql("select count(*) from public.rejections")[0][0]
    ok("refusals were logged", logged >= 5, str(logged))

    # ---------- a crash right after the ACK, then the trust engine ----------
    os.kill(server.pid, signal.SIGKILL)
    server.wait()
    print("     server killed with SIGKILL")
    server = start()
    ok("retry after restart gets the earlier ACK", http.post(API + "/reports", content=lines[1]).json().get("status") == "duplicate")
    url = f"{API}/reports/{cars[0].id}/{cars[0].id}-1-1"
    for _ in range(120):
        rr = http.get(url, headers=A).json()
        if rr["evaluation"] != "pending":
            break
        time.sleep(0.5)
    ok("trust engine evaluated the report", rr["evaluation"] == "evaluated" and rr["hazard_ids"], str(rr["evaluation"]))
    ok("other user cannot read the report", http.get(url, headers=B).status_code == 403)
    runs = sql("select count(*) from public.evaluation_runs")[0][0]
    ok("evaluation run recorded", runs >= 1, str(runs))
    for state, sources, net, sybil in sql("select state, independent_sources, net_evidence, sybil_suspected from public.hazards"):
        print(f"     hazard: state={state} independent_sources={sources} net_evidence={net:.2f} sybil_suspected={sybil}")

    # ---------- alerts ----------
    plat, plon = offset(LAT, LON, 0, -450)
    ask = {"lat": plat, "lon": plon, "heading_deg": 90}
    first = http.post(API + "/hazards/ahead", headers=B, json=ask).json()
    again = http.post(API + "/hazards/ahead", headers=B, json=ask).json()
    ok("hazard 450 m ahead alerts once", bool(first.get("hazards")) and first["hazards"][0]["alert"] and not again["hazards"][0]["alert"])
    ok("same hazard still alerts the other user", bool(http.post(API + "/hazards/ahead", headers=A, json=ask).json()["hazards"][0]["alert"]))
    if first.get("hazards"):
        print("     state:", first["hazards"][0]["state"], "distance:", first["hazards"][0]["distance_m"])
    hz = first["hazards"][0]["hazard_id"]
    plat, plon = offset(LAT, LON, 0, 300)
    ok("hazard already passed gives no alert", not http.post(API + "/hazards/ahead", headers=B, json={"lat": plat, "lon": plon, "heading_deg": 90}).json()["hazards"])
    ok("asking for alerts stores no position", sql("select count(*) from public.beacons")[0][0] == beacons_stored)

    # ---------- straight at Supabase's public API, the way someone with the app's public key could ----------
    h_anon = {"apikey": ANON, "Authorization": "Bearer " + ANON}
    R_a, R_b = {"apikey": ANON, **A}, {"apikey": ANON, **B}
    ok("PostgREST: owner reads own reports", len(http.get(REST + "/reports?select=device_id", headers=R_a).json()) == 3)
    ok("PostgREST: other user reads none", http.get(REST + "/reports?select=device_id", headers=R_b).json() == [])
    ok("PostgREST: anon key refused", http.get(REST + "/hazards?select=*", headers=h_anon).status_code in (401, 403))
    ok("PostgREST: driver reads published hazard", len(http.get(f"{REST}/hazards?select=hazard_id&hazard_id=eq.{hz}", headers=R_b).json()) == 1)
    ok("PostgREST: rpc nearby_hazards works for a user", len(http.post(REST + "/rpc/nearby_hazards", headers=R_b, json={"p_lat": LAT, "p_lon": LON, "p_radius_m": 100, "p_states": ["corroborated", "confirmed"]}).json()) >= 1)
    ok("PostgREST: anon cannot call rpc", http.post(REST + "/rpc/nearby_hazards", headers=h_anon, json={"p_lat": LAT, "p_lon": LON, "p_radius_m": 100}).status_code in (401, 403))
    for name, resp in (("change a hazard", http.patch(f"{REST}/hazards?hazard_id=eq.{hz}", headers=R_b, json={"state": "suppressed"})),
                       ("raise reputation", http.patch(f"{REST}/devices?device_id=eq.{cars[0].id}", headers=R_a, json={"good": 999})),
                       ("become admin", http.patch(f"{REST}/profiles?user_id=eq.{me.json()['user_id']}", headers=R_a, json={"role": "admin"})),
                       ("insert evidence", http.post(f"{REST}/hazard_evidence", headers=R_a, json={"hazard_id": hz, "device_id": "x", "event_id": "y", "c": 1, "g": 1, "h": 1, "f": 1, "weight": 1})),
                       ("read pairing hash", http.get(f"{REST}/devices?select=pairing_code_hash", headers=R_a))):
        ok(f"PostgREST: user cannot {name}", resp.status_code in (401, 403), str(resp.status_code))
    for schema in LEGACY_SCHEMAS:
        resp = http.get(REST + "/devices?select=*", headers={**R_a, "Accept-Profile": schema})
        ok(f"PostgREST: {schema} not reachable", resp.status_code != 200, str(resp.status_code))
finally:
    server.terminate()
    server.wait()
print(f"Run {RUN}. Test rows were kept: devices and users named e2etest-{RUN}-*")
print("FAILED:", ok.failed)
sys.exit(1 if ok.failed else 0)
