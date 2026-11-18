"""The same behaviour on real Supabase PostgreSQL + PostGIS, connected as the restricted backend role, plus what only
a real database can show: concurrent copies, restarts, constraints, RLS and the geographic search.

Needs a throwaway database, because the public schema is wiped and rebuilt from the migrations:
    docker run -d --name icvsp-pg -e POSTGRES_PASSWORD=postgres -p 54329:5432 supabase/postgres:17.11.0.003
    ICVSP_TEST_DATABASE_URL=postgresql://postgres:postgres@127.0.0.1:54329/postgres python3 -m unittest -v
Skipped without that variable; CI always sets it."""
import json
import os
import time
import unittest
import uuid
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit

from fastapi.testclient import TestClient

from icvsp_backend.app import create_app
from icvsp_backend.store_errors import StoreUnavailable

from tests import test_api, test_backend
from tests.test_api import ADMIN, T0, Car
from tests.test_backend import LAT, LON, FakeVerifier, at

ADMIN_URL = os.environ.get("ICVSP_TEST_DATABASE_URL")
MIGRATIONS = Path(__file__).resolve().parents[2] / "database/supabase/migrations"
BACKEND_PASSWORD = "test-only-not-a-secret"
TABLES = ("reputation_events, alert_deliveries, hazard_evidence, hazard_passes, hazards, evaluation_runs, rejections, "
          "beacons, reports, device_counters, user_devices, devices, profiles")
if ADMIN_URL:
    import psycopg
    from icvsp_backend.pg_store import PgStore


def backend_url() -> str:
    parts = urlsplit(ADMIN_URL)
    return urlunsplit(parts._replace(netloc=f"icvsp_backend:{BACKEND_PASSWORD}@{parts.hostname}:{parts.port}"))


def admin_connection():
    return psycopg.connect(ADMIN_URL, autocommit=True)


def setUpModule():
    if not ADMIN_URL:
        return
    if urlsplit(ADMIN_URL).hostname not in ("localhost", "127.0.0.1"):
        raise RuntimeError("ICVSP_TEST_DATABASE_URL must be local: these tests wipe the public schema")
    with admin_connection() as db:
        db.execute("drop trigger if exists on_auth_user_created on auth.users")
        db.execute("drop schema if exists public cascade")
        db.execute("create schema public")
        db.execute("grant usage on schema public to anon, authenticated, service_role, postgres")
        for f in sorted(MIGRATIONS.glob("*.sql")):
            db.execute(f.read_text())
        db.execute(f"alter role icvsp_backend login password '{BACKEND_PASSWORD}'")


@unittest.skipUnless(ADMIN_URL, "ICVSP_TEST_DATABASE_URL not set")
class PgTest(unittest.TestCase):
    """Mixed in first: gives any test case a clean database and a store using the backend role."""

    def make_store(self):
        with admin_connection() as db:
            db.execute(f"truncate {TABLES} cascade")
            db.execute("delete from auth.users")
        store = PgStore(backend_url())
        store.add_profile = self._add_profile
        return store

    def _add_profile(self, user_id: str, role: str = "driver") -> None:
        """A real Supabase user: the trigger creates the profile, an administrator changes the role."""
        with admin_connection() as db:
            db.execute("insert into auth.users (id, email) values (%s, %s)", (user_id, f"{user_id}@test.invalid"))
            db.execute("update public.profiles set role = %s where user_id = %s", (role, user_id))


class PgReports(PgTest, test_api.Reports):
    def setUp(self):
        super().setUp()
        self.client.app.state.service.eval_on_read = True


class PgHazards(PgTest, test_api.Hazards):
    def setUp(self):
        super().setUp()
        self.client.app.state.service.eval_on_read = True


class PgDelivery(PgTest, test_backend.Delivery):
    pass


class PgEvaluation(PgTest, test_backend.Evaluation):
    pass


class PgAhead(PgTest, test_backend.Ahead):
    pass


class PgAccess(PgTest, test_backend.Access):
    pass


class PgContract(PgTest, test_backend.Contract):
    pass


class Case(PgTest, test_backend.Case):
    pass


class Concurrency(Case):
    def test_simultaneous_copies_make_one_record_and_one_pending_evaluation(self):
        """Phone retry and upload arriving together: the database, not luck, makes it one report."""
        c = self.car("car-a")
        line = c.report(T0, LAT, LON)
        with ThreadPoolExecutor(16) as pool:
            answers = list(pool.map(lambda _: self.post(line), range(32)))
        self.assertEqual({a.status_code for a in answers}, {201})
        statuses = [a.json()["status"] for a in answers]
        self.assertEqual(statuses.count("accepted"), 1)
        self.assertEqual(statuses.count("duplicate"), 31)
        self.assertEqual(self.store.count("reports"), 1)
        self.assertEqual(self.store.counters("car-a")["reports_stored"], 1)

    def test_simultaneous_different_content_with_one_id_has_one_winner(self):
        c = self.car("car-a")
        lines = []
        for k in range(8):
            c.seq = 0
            lines.append(c.report(T0 + k, LAT, LON))
        with ThreadPoolExecutor(8) as pool:
            codes = [a.status_code for a in pool.map(self.post, lines)]
        self.assertEqual(sorted(codes), [201] + [409] * 7)
        self.assertEqual(self.store.count("reports"), 1)

    def test_two_evaluators_at_once_do_not_double_count(self):
        self.hazard(("car-a", "car-b", "car-c"))
        before = self.store.reputations()["car-a"]
        with ThreadPoolExecutor(6) as pool:
            list(pool.map(lambda _: self.svc.evaluate(self.clock.t, settle=True), range(6)))
        self.assertEqual(self.store.reputations()["car-a"], (before[0] + 1, before[1]))

    def test_second_evaluator_steps_aside_while_the_first_holds_the_lock(self):
        with self.store.transaction():
            self.assertTrue(self.store.try_evaluator_lock())
            with ThreadPoolExecutor(1) as pool:
                other = pool.submit(self.svc.evaluate, self.clock.t).result()
            self.assertTrue(other["busy"])


class Restart(Case):
    def test_duplicate_protection_and_counters_survive_a_server_restart(self):
        c = self.car("car-a")
        line = c.report(T0, LAT, LON)
        self.post(line)
        self.store.close()
        self.store = PgStore(backend_url())
        self.addCleanup(self.store.close)
        client = TestClient(create_app(self.store, self.clock, admin_token="test-admin", verifier=FakeVerifier()))
        self.assertEqual(client.post("/reports", content=line).json()["status"], "duplicate")
        self.assertEqual(self.store.counters("car-a")["highest_sequence"], 1)
        self.assertEqual(self.store.count("reports"), 1)

    def test_pending_reports_are_finished_after_a_restart(self):
        c = self.car("car-a")
        self.drive("car-a", T0 - 10)
        self.post(c.report(T0, LAT, LON))
        self.store.close()                     # stopped between the ACK and the evaluation
        self.store = PgStore(backend_url())
        self.addCleanup(self.store.close)
        client = TestClient(create_app(self.store, self.clock, admin_token="test-admin", verifier=FakeVerifier()))
        self.assertEqual(self.store.pending_count(), 1)
        self.assertEqual(client.post("/admin/evaluate", headers=ADMIN).status_code, 200)
        self.assertEqual(self.store.pending_count(), 0)

    def test_database_down_gives_retryable_answer_then_the_retry_is_stored(self):
        c = self.car("car-a")
        line = c.report(T0, LAT, LON)
        self.store.close()
        r = TestClient(self.client.app, raise_server_exceptions=True).post("/reports", content=line)
        self.assertEqual((r.status_code, r.json()["retryable"]), (503, True))
        self.assertEqual(self.client.get("/ready").status_code, 503)
        self.store = PgStore(backend_url())
        self.addCleanup(self.store.close)
        client = TestClient(create_app(self.store, self.clock, admin_token="test-admin", verifier=FakeVerifier()))
        self.assertEqual(client.post("/reports", content=line).json()["status"], "accepted")

    def test_closed_store_raises_the_retryable_error(self):
        self.store.close()
        with self.assertRaises(StoreUnavailable):
            self.store.count("reports")


class Constraints(Case):
    def row(self, **changes) -> dict:
        return {"device_id": "car-a", "event_id": "car-a-1-1", "boot_id": 1, "sequence": 1, "type": "pothole",
                "severity": "high", "detection_confidence": 0.9, "gps_valid": True, "latitude": LAT, "longitude": LON,
                "position_accuracy_m": 3.0, "line": "{}", **changes}

    def insert(self, **changes):
        row = self.row(**changes)
        with admin_connection() as db:
            db.execute(f"""insert into public.reports ({', '.join(row)}, detected_at, sent_at)
                           values ({', '.join(['%s'] * len(row))}, now(), now())""", list(row.values()))

    def test_database_itself_refuses_repeats_and_bad_values(self):
        """Even code that skips the API cannot store these."""
        self.car("car-a")
        self.insert()
        for changes in ({},                                                     # same device + event_id
                        {"event_id": "other"},                                  # same device + boot + sequence
                        {"event_id": "b", "sequence": 2, "latitude": 91.0},
                        {"event_id": "c", "sequence": 3, "detection_confidence": 1.5},
                        {"event_id": "d", "sequence": 4, "type": "elephant"},
                        {"event_id": "e", "sequence": 5, "device_id": "no-such-car"},
                        {"event_id": "f", "sequence": 6, "latitude": None},     # gps_valid needs a position
                        {"event_id": "g", "sequence": 7, "line": "x" * 3000}):
            with self.assertRaises(psycopg.errors.IntegrityError, msg=str(changes)):
                self.insert(**changes)

    def test_device_with_reports_cannot_be_deleted(self):
        self.car("car-a")
        self.insert()
        with admin_connection() as db, self.assertRaises(psycopg.errors.IntegrityError):
            db.execute("delete from public.devices where device_id = 'car-a'")

    def test_same_evidence_cannot_be_stored_twice(self):
        self.hazard()
        with admin_connection() as db:
            row = db.execute("select * from public.hazard_evidence limit 1").fetchone()
            with self.assertRaises(psycopg.errors.IntegrityError):
                db.execute("insert into public.hazard_evidence values (%s, %s, %s, %s, %s, %s, %s, %s)", row)

    def test_backend_role_cannot_rewrite_history_or_the_schema(self):
        store = self.store
        c = self.car("car-a")
        self.post(c.report(T0, LAT, LON))
        for sql in ("truncate public.reports", "drop table public.reports", "delete from public.devices",
                    "delete from public.profiles", "delete from public.device_counters",
                    "update public.rejections set reason = 'x'", "alter table public.reports disable row level security",
                    "select * from auth.users"):
            with self.assertRaises(psycopg.errors.Error, msg=sql):
                store._x(sql)

    def test_evaluation_run_records_the_configuration_hash(self):
        self.hazard()
        with admin_connection() as db:
            self.assertEqual(db.execute("select distinct config_hash from public.evaluation_runs").fetchall(),
                             [(self.svc.config_hash,)])


class Geo(Case):
    def test_postgis_search_finds_by_radius_and_state_and_validity(self):
        self.hazard()
        near = self.store.nearby_hazards(*at(0, -300), 400, ("corroborated",), self.clock.t)
        self.assertEqual(len(near), 1)
        self.assertAlmostEqual(near[0]["distance_m"], 300, delta=2)
        self.assertEqual(self.store.nearby_hazards(*at(0, -300), 200, ("corroborated",), self.clock.t), [])
        self.assertEqual(self.store.nearby_hazards(*at(0, -300), 400, ("confirmed",), self.clock.t), [])
        self.assertEqual(self.store.nearby_hazards(*at(0, -300), 400, ("corroborated",), self.clock.t + 3 * 86400), [])

    def test_search_uses_the_geographic_index(self):
        with admin_connection() as db:
            db.execute("begin")
            db.execute("set local enable_seqscan = off")
            plan = db.execute("""explain select 1 from public.hazards h where extensions.st_dwithin(h.location,
                extensions.st_setsrid(extensions.st_makepoint(31.2, 30.0), 4326)::extensions.geography, 1000)""").fetchall()
            db.execute("rollback")
        self.assertIn("hazards_location", str(plan))


class RowLevelSecurity(Case):
    """Connects the way a phone using the Supabase client does: role authenticated with its user's claims, or anon."""

    def setUp(self):
        super().setUp()
        self.clock.t = time.time()     # RLS compares with the database's real clock

    def as_user(self, user_id: str | None):
        db = admin_connection()
        self.addCleanup(db.close)
        # both spellings: the bare image's auth.uid() reads the first, a full Supabase project also reads the second
        db.execute("select set_config('request.jwt.claim.sub', %s, false), set_config('request.jwt.claims', %s, false)",
                   (user_id or "", json.dumps({"sub": user_id, "role": "authenticated"}) if user_id else "{}"))
        db.execute("set role " + ("authenticated" if user_id else "anon"))
        return db

    def user(self, role: str = "driver") -> tuple[str, dict]:
        headers = self.new_user(role)
        return self.uid(headers), headers

    def live_hazard(self) -> str:
        now = self.clock.t
        for i, name in enumerate(("car-a", "car-b")):
            c = self.car(name)
            t = now - 60 + 30 * i
            self.drive(name, t - 10)
            self.assertEqual(self.post(c.report(t, LAT, LON)).status_code, 201)
        self.svc.evaluate(now)
        return self.store.nearby_hazards(LAT, LON, 100, ("corroborated", "confirmed"), now)[0]["hazard_id"]

    def test_user_reads_only_their_own_rows(self):
        alice, alice_h = self.user()
        bob, bob_h = self.user()
        for headers, name in ((alice_h, "alice-car"), (bob_h, "bob-car")):
            c = self.owned_car(headers, name)
            self.assertEqual(self.post(c.report(self.clock.t, LAT, LON)).status_code, 201)
        db = self.as_user(alice)
        for sql, want in (("select device_id from public.reports", [("alice-car",)]),
                          ("select device_id from public.devices", [("alice-car",)]),
                          ("select device_id from public.user_devices", [("alice-car",)])):
            self.assertEqual(db.execute(sql).fetchall(), want, sql)
        self.assertEqual(db.execute("select user_id::text from public.profiles").fetchall(), [(alice,)])

    def test_user_cannot_change_trust_results_reputation_or_their_own_role(self):
        alice, headers = self.user()
        hazard_id = self.live_hazard()
        self.owned_car(headers, "alice-car")
        db = self.as_user(alice)
        for sql in (f"update public.hazards set state = 'confirmed' where hazard_id = '{hazard_id}'",
                    f"delete from public.hazards where hazard_id = '{hazard_id}'",
                    "update public.devices set good = 1000 where device_id = 'alice-car'",
                    f"update public.profiles set role = 'admin' where user_id = '{alice}'",
                    "update public.reports set evaluation_status = 'evaluated'",
                    "insert into public.user_devices values (auth.uid(), 'car-a')",
                    f"insert into public.hazard_evidence values ('{hazard_id}', 'alice-car', 'x', 1, 1, 1, 1, 1)"):
            with self.assertRaises(psycopg.errors.InsufficientPrivilege, msg=sql):
                db.execute(sql)

    def test_private_columns_and_tables_are_closed_to_users(self):
        alice, headers = self.user()
        self.owned_car(headers, "alice-car")
        db = self.as_user(alice)
        for sql in ("select pairing_code_hash from public.devices", "select * from public.beacons",
                    "select * from public.device_counters", "select * from public.reputation_events",
                    "select * from public.alert_deliveries", "select * from public.evaluation_runs"):
            with self.assertRaises(psycopg.errors.InsufficientPrivilege, msg=sql):
                db.execute(sql)
        self.assertEqual(db.execute("select * from public.rejections").fetchall(), [])
        self.assertEqual(db.execute("select * from public.hazard_evidence").fetchall(), [])

    def test_anonymous_client_sees_nothing_at_all(self):
        self.live_hazard()
        db = self.as_user(None)
        for sql in ("select * from public.hazards", "select * from public.reports", "select * from public.profiles",
                    f"select * from public.nearby_hazards({LAT}, {LON}, 1000)"):
            with self.assertRaises(psycopg.errors.InsufficientPrivilege, msg=sql):
                db.execute(sql)

    def test_driver_gets_only_published_valid_hazards_through_the_search_function(self):
        alice, _ = self.user()
        hazard_id = self.live_hazard()
        all_states = "array['suppressed','unconfirmed','corroborated','confirmed','expired']"
        db = self.as_user(alice)
        ask = f"select hazard_id::text from public.nearby_hazards({LAT}, {LON}, 1000, {all_states})"
        self.assertEqual(db.execute(ask).fetchall(), [(hazard_id,)])
        with admin_connection() as admin:
            admin.execute("update public.hazards set state = 'suppressed'")
        self.assertEqual(db.execute(ask).fetchall(), [])
        with admin_connection() as admin:
            admin.execute("update public.hazards set state = 'confirmed', expires_at = now() - interval '1 minute'")
        self.assertEqual(db.execute("select * from public.hazards").fetchall(), [])

    def test_administrator_policy_opens_evidence_for_reading_only(self):
        admin, _ = self.user("admin")
        hazard_id = self.live_hazard()
        db = self.as_user(admin)
        self.assertEqual(len(db.execute("select * from public.hazard_evidence").fetchall()), 2)
        with self.assertRaises(psycopg.errors.InsufficientPrivilege):
            db.execute(f"delete from public.hazard_evidence where hazard_id = '{hazard_id}'")

    def test_new_supabase_user_gets_a_driver_profile_automatically(self):
        user_id = str(uuid.uuid4())
        with admin_connection() as db:
            db.execute("insert into auth.users (id, email) values (%s, 'new@test.invalid')", (user_id,))
            self.assertEqual(db.execute("select role from public.profiles where user_id = %s", (user_id,)).fetchone(),
                             ("driver",))

    def test_supabase_client_roles_cannot_call_the_trigger_function(self):
        alice, _ = self.user()
        db = self.as_user(alice)
        with self.assertRaises(psycopg.errors.InsufficientPrivilege):
            db.execute("select public.handle_new_user()")

    def test_pairing_a_device_needs_the_code_even_through_the_backend(self):
        c = Car("car-z")
        self.client.post("/devices", json={"device_id": "car-z", "public_key": c.public_hex}, headers=ADMIN)
        alice, headers = self.user()
        answer = self.client.post("/me/devices", json={"device_id": "car-z", "pairing_code": "guess"}, headers=headers)
        self.assertEqual(answer.status_code, 403)
        self.assertEqual(self.store.user_devices(alice), [])
