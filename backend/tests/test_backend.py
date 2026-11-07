"""Backend behaviour from the integration guide, section 11: delivery and retries, evaluation after a crash, hazards
ahead of the user, and who may see what. Every class also runs on real Supabase PostgreSQL (test_postgres.py).
Run:  cd backend && python3 -m unittest -v"""
import json
import sqlite3
import ssl
import tempfile
import time
import unittest
import uuid
from unittest import mock

import jwt
from cryptography.hazmat.primitives.asymmetric import ec
from fastapi.testclient import TestClient

from icvsp_backend.app import create_app
from icvsp.geo import offset
from icvsp_backend.auth import AuthUnavailable, InvalidToken, SupabaseVerifier
from icvsp_backend.store import Store
from icvsp_backend.store_errors import StoreUnavailable

from tests.test_api import ADMIN, GOLDEN, T0, Base, Car

LAT, LON = 30.0444, 31.2357
HOUR, DAY = 3600, 24 * 3600


def at(north_m: float, east_m: float) -> tuple[float, float]:
    """A place relative to the hazard, which lies on an east-west road and was reported by cars driving east."""
    return offset(LAT, LON, north_m, east_m)


class FakeVerifier:
    """The token is the user's ID (real tokens are tested in Tokens below)."""

    def verify(self, token: str) -> str:
        if token == "bad":
            raise InvalidToken("bad token")
        return token


class Case(Base):
    def setUp(self):
        super().setUp()
        self.client = TestClient(create_app(self.store, self.clock, admin_token="test-admin", verifier=FakeVerifier(),
                                            allow_anonymous=False))
        self.svc = self.client.app.state.service
        self.svc.eval_on_read = False     # as in production: the worker evaluates, reads only read

    def new_user(self, role: str = "driver") -> dict:
        """Returns the headers of a new signed-in user."""
        user_id = str(uuid.uuid4())
        self.store.add_profile(user_id, role)
        return {"Authorization": f"Bearer {user_id}"}

    def uid(self, headers: dict) -> str:
        return headers["Authorization"].split()[1]

    def car(self, device_id: str, vetted: bool = True, boot: int = 1) -> Car:
        c = Car(device_id, boot)
        self.register(device_id, c.public_hex, vetted=vetted)
        return c

    def owned_car(self, headers: dict, device_id: str) -> Car:
        c = Car(device_id)
        r = self.client.post("/devices", json={"device_id": device_id, "public_key": c.public_hex}, headers=ADMIN)
        claim = {"device_id": device_id, "pairing_code": r.json()["pairing_code"]}
        self.assertEqual(self.client.post("/me/devices", json=claim, headers=headers).status_code, 201)
        return c

    def drive(self, vid: str, t0: float, seconds: int = 60):
        """1 Hz positions of a car driving east over the hazard, so its report is plausible."""
        self.svc.add_beacons([{"vehicle_id": vid, "t": t0 + k, "lat": at(0, -110 + 11 * k)[0],
                               "lon": at(0, -110 + 11 * k)[1], "speed_mps": 11.0, "heading_deg": 90.0}
                              for k in range(seconds)])

    def hazard(self, cars: tuple[str, ...] = ("car-a", "car-b")) -> None:
        """Trusted cars report one pothole and the evaluator runs: a warning-level hazard exists afterwards."""
        self.cars = {}
        for i, name in enumerate(cars):
            c = self.cars[name] = self.car(name)
            t = T0 + 30 * i
            self.drive(name, t - 10)
            self.assertEqual(self.post(c.report(t, LAT, LON)).status_code, 201)
        self.clock.t = T0 + 30 * len(cars)
        self.svc.evaluate(self.clock.t)


class Delivery(Case):
    def test_sent_report_is_stored_and_read_back(self):
        """First integration check of the guide: send a report, it is stored, read it back through the API."""
        c = self.car("car-a")
        ack = self.post(c.report(T0, LAT, LON)).json()
        self.assertEqual((ack["status"], ack["ack"]), ("accepted", "stored"))
        r = self.client.get("/reports/car-a/car-a-1-1", headers=self.new_user("admin"))
        self.assertEqual(r.status_code, 200, r.text)
        self.assertEqual((r.json()["type"], r.json()["latitude"], r.json()["severity"]), ("pothole", LAT, "high"))

    def test_ack_means_stored_not_that_the_hazard_is_confirmed(self):
        c = self.car("car-a")
        self.assertEqual(self.post(c.report(T0, LAT, LON)).json()["evaluation"], "pending")
        admin = self.new_user("admin")
        self.assertEqual(self.client.get("/reports/car-a/car-a-1-1", headers=admin).json()["evaluation"], "pending")
        self.drive("car-a", T0 - 10)
        self.svc.evaluate(self.clock.t)
        read = self.client.get("/reports/car-a/car-a-1-1", headers=admin).json()
        self.assertEqual(read["evaluation"], "evaluated")
        self.assertEqual(len(read["hazard_ids"]), 1)

    def test_lost_ack_retry_gets_the_earlier_ack_and_stores_nothing(self):
        c = self.car("car-a")
        line = c.report(T0, LAT, LON)
        first = self.post(line).json()
        self.clock.t += 500
        retry = self.post(line)
        self.assertEqual(retry.status_code, 201)
        self.assertEqual(retry.json()["status"], "duplicate")
        self.assertEqual(retry.json()["stored_at"], first["stored_at"])
        self.assertEqual(self.store.count("reports"), 1)
        self.assertEqual(self.store.counters("car-a")["reports_stored"], 1)

    def test_retry_long_after_the_freshness_window_still_gets_its_ack(self):
        """Stored, ACK lost, phone offline for days: it must hear "stored" so it can delete its copy."""
        c = self.car("car-a")
        line = c.report(T0, LAT, LON)
        self.post(line)
        self.clock.t = T0 + 5 * DAY
        self.assertEqual(self.post(line).json()["status"], "duplicate")

    def test_same_id_with_other_content_is_refused_as_final(self):
        c = self.car("car-a")
        self.post(c.report(T0, LAT, LON))
        c.seq = 0
        r = self.post(c.report(T0, LAT + 0.001, LON))
        self.assertEqual(r.status_code, 409)
        self.assertEqual((r.json()["code"], r.json()["retryable"]), ("id_conflict", False))
        self.assertEqual(self.store.count("reports"), 1)

    def test_rejections_are_logged_with_code_and_reason(self):
        c = self.car("car-a")
        self.post(c.report(T0, LAT, LON).replace(b'"high"', b'"low"'))         # signature no longer matches
        self.clock.t += 1
        self.client.post("/devices/car-a/revoke", headers=ADMIN)
        self.post(c.report(T0, LAT, LON))                                      # valid signature, revoked device
        codes = [r["code"] for r in self.store.rejections(10)]
        self.assertEqual(codes, ["bad_signature", "device_revoked"])
        self.assertEqual(self.store.count("reports"), 0)

    def test_storage_failure_says_retry_and_the_same_line_then_succeeds(self):
        """Connection lost before saving: nothing stored, a retryable answer, no rejection."""
        c = self.car("car-a")
        line = c.report(T0, LAT, LON)
        with mock.patch.object(self.store, "add_report", side_effect=StoreUnavailable("down")):
            r = self.post(line)
        self.assertEqual(r.status_code, 503)
        self.assertTrue(r.json()["retryable"])
        self.assertIn("retry-after", r.headers)
        self.assertEqual(self.store.count("reports"), 0)
        self.assertEqual(self.post(line).json()["status"], "accepted")

    def test_rejection_answer_survives_a_failing_rejection_log(self):
        c = self.car("car-a")
        with mock.patch.object(self.store, "reject", side_effect=StoreUnavailable("down")):
            self.assertEqual(self.post(c.report(T0, LAT, LON).replace(b'"high"', b'"low"')).status_code, 401)

    def test_delayed_report_within_its_window_is_accepted(self):
        """No internet for a day and a half: a true report must not be refused for being late."""
        c = self.car("car-a")
        self.clock.t = T0 + 40 * HOUR
        self.assertEqual(self.post(c.report(T0, LAT, LON)).json()["status"], "accepted")

    def test_too_old_and_future_reports_are_refused(self):
        c = self.car("car-a")
        self.clock.t = T0 + 49 * HOUR
        old = self.post(c.report(T0, LAT, LON))
        self.assertEqual((old.status_code, old.json()["code"]), (422, "stale"))
        future = self.post(c.report(self.clock.t + 300, LAT, LON))
        self.assertEqual((future.status_code, future.json()["code"]), (422, "future"))

    def test_freshness_window_follows_the_hazard_type(self):
        c = self.car("car-a")
        self.clock.t = T0 + 3 * DAY
        self.assertEqual(self.post(c.report(T0, LAT, LON, type_="speed_bump")).status_code, 201)

    def test_device_restart_and_out_of_order_delivery_are_accepted(self):
        """After a restart the boot counter rises; older queued reports may still arrive afterwards."""
        c = self.car("car-a", boot=3)
        late = [c.report(T0, LAT, LON), c.report(T0 + 1, LAT, LON)]
        c.boot, c.seq = 4, 0
        self.assertEqual(self.post(c.report(T0 + 2, LAT, LON)).status_code, 201)
        for line in reversed(late):
            self.assertEqual(self.post(line).json()["status"], "accepted")
        counters = self.store.counters("car-a")
        self.assertEqual((counters["highest_boot_id"], counters["highest_sequence"], counters["reports_stored"]),
                         (4, 1, 3))

    def test_replay_after_cleanup_is_refused_as_too_old(self):
        """Retention never reopens a hole: once a report is deleted, it is already outside the freshness window."""
        c = self.car("car-a")
        line = c.report(T0, LAT, LON)
        self.post(line)
        self.clock.t = T0 + 16 * DAY
        self.svc.purge(self.clock.t)
        self.assertEqual(self.store.count("reports"), 0)
        r = self.post(line)
        self.assertEqual((r.status_code, r.json()["code"]), (422, "stale"))

    def test_cleanup_keeps_reports_inside_the_longest_window(self):
        c = self.car("car-a")
        line = c.report(T0, LAT, LON)
        self.post(line)
        self.drive("car-a", T0)
        self.clock.t = T0 + 14 * DAY
        self.svc.purge(self.clock.t)
        self.assertEqual(self.store.count("beacons"), 0)
        self.assertEqual(self.post(line).json()["status"], "duplicate")

    def test_hostile_bodies_get_a_clear_refusal_not_a_server_error(self):
        c = self.car("car-a")
        good = json.loads(c.report(T0, LAT, LON))
        bodies = [
            b"[" * 1500,
            json.dumps({**good, "utc_ms": 2**70}).encode(),
            b'{"event":{},"sender":"a\\u0000b","msg_id":"x","utc_ms":1,"signature":"' + b"00" * 64 + b'"}',
            b'{"event":{},"sender":"\\ud800","msg_id":"x","utc_ms":1,"signature":"' + b"00" * 64 + b'"}',
            b'{"event":{},"sender":[1],"msg_id":"x","utc_ms":1,"signature":"' + b"00" * 64 + b'"}',
            b"\xff\xfe",
        ]
        for body in bodies:
            self.assertEqual(self.post(body).status_code, 400, body[:60])
        self.assertEqual(self.post(b"x" * 100000).status_code, 413)

    def test_signed_counter_too_large_for_the_database_is_refused(self):
        c = self.car("car-a")
        r = self.post(c.report(T0, LAT, LON, boot_id=2**70))
        self.assertEqual((r.status_code, r.json()["code"]), (422, "schema"))


class Evaluation(Case):
    def test_copied_traces_stay_one_source_after_the_raw_position_window(self):
        """Three identities moving as one must not become three sources just because time passed."""
        for i, name in enumerate(("car-a", "car-b", "car-c")):
            c = self.car(name)
            self.drive(name, T0 - 10)
            self.post(c.report(T0 + i, LAT, LON))
        self.clock.t = T0 + 60
        before = self.svc.decide(self.clock.t)[0]
        self.assertTrue(before.sybil_suspected)
        self.assertEqual((before.state.value, before.independent_sources), ("unconfirmed", 1))
        self.clock.t = T0 + 2 * HOUR + 100
        self.svc.purge(self.clock.t)
        after = self.svc.decide(self.clock.t)[0]
        self.assertTrue(after.sybil_suspected)
        self.assertEqual((after.state.value, after.independent_sources), ("unconfirmed", 1))

    def test_drive_past_evidence_survives_the_raw_position_window(self):
        """A hazard that drivers disproved does not come back when the raw positions are cleaned up."""
        for i, name in enumerate(("car-a", "car-b", "car-c", "car-d", "car-e", "car-f")):
            c = self.car(name)
            self.drive(name, T0 + 100 * i)
            if i < 2:
                self.clock.t = T0 + 100 * i + 20
                self.assertEqual(self.post(c.report(T0 + 100 * i + 10, LAT, LON)).status_code, 201)
        self.clock.t = T0 + 700
        before = self.svc.decide(self.clock.t)[0]
        self.assertEqual((before.state.value, len(before.contra)), ("suppressed", 4))
        self.clock.t = T0 + 2 * HOUR + 700
        self.svc.purge(self.clock.t)
        after = self.svc.decide(self.clock.t)[0]
        self.assertEqual((after.state.value, len(after.contra)), ("suppressed", 4))

    def test_cleanup_keeps_only_positions_near_reports_still_counted(self):
        c = self.car("car-a")
        self.post(c.report(T0, LAT, LON))
        rows = {"near": (T0 + 5, *at(0, 50)), "far": (T0 + 6, *at(5000, 0)), "before": (T0 - HOUR, *at(0, 50))}
        self.svc.add_beacons([{"vehicle_id": "car-a", "t": t, "lat": lat, "lon": lon, "speed_mps": 11.0,
                               "heading_deg": 90.0} for t, lat, lon in rows.values()])
        self.clock.t = T0 + 3 * HOUR
        self.svc.purge(self.clock.t)
        self.assertEqual([round(b["t"]) for b in self.store.beacons_since(0)], [round(T0 + 5)])
        self.clock.t = T0 + 2 * DAY + HOUR                 # a pothole report counts for 48 h
        self.svc.purge(self.clock.t)
        self.assertEqual(self.store.count("beacons"), 0)

    def test_positions_near_a_speed_bump_report_are_kept_for_its_longer_window(self):
        c = self.car("car-a")
        self.post(c.report(T0, LAT, LON, type_="speed_bump"))
        self.drive("car-a", T0 - 5, seconds=10)
        self.clock.t = T0 + 13 * DAY
        self.svc.purge(self.clock.t)
        self.assertEqual(self.store.count("beacons"), 10)
        self.clock.t = T0 + 15 * DAY
        self.svc.purge(self.clock.t)
        self.assertEqual(self.store.count("beacons"), 0)

    def test_same_event_id_on_two_devices_counts_for_both(self):
        """One device choosing another's event ID cannot get that device's real report thrown out."""
        a, b = self.car("car-a"), self.car("car-b")
        self.drive("car-a", T0 + 20)
        self.drive("car-b", T0 - 10)
        self.post(b.report(T0, LAT, LON, event_id="car-a-1-1"))
        self.post(a.report(T0 + 30, LAT, LON))
        self.clock.t = T0 + 60
        self.svc.evaluate(self.clock.t)
        for device in ("car-a", "car-b"):
            self.assertEqual(self.store.report(device, "car-a-1-1")["evaluation_status"], "evaluated", device)
        self.assertEqual(self.svc.decide(self.clock.t)[0].independent_sources, 2)

    def test_crash_after_saving_leaves_everything_pending_then_finishes_counting_once(self):
        """Processing stops after the report is stored: nothing half written, and the rerun counts each once."""
        self.hazard(("car-a", "car-b", "car-c"))
        self.client.post("/admin/settle", headers=ADMIN)
        c = self.car("car-d")
        self.drive("car-d", T0 + 70)
        self.post(c.report(T0 + 80, LAT, LON))
        self.clock.t = T0 + 100
        before = self.store.reputations()["car-d"]
        with mock.patch.object(self.store, "close_pending_before", side_effect=RuntimeError("power cut")):
            with self.assertRaises(RuntimeError):
                self.svc.evaluate(self.clock.t, settle=True)
        self.assertEqual(self.store.report("car-d", "car-d-1-1")["evaluation_status"], "pending")
        self.assertEqual(self.store.reputations()["car-d"], before)
        first = self.svc.evaluate(self.clock.t, settle=True)
        self.assertEqual(self.store.report("car-d", "car-d-1-1")["evaluation_status"], "evaluated")
        self.assertTrue(any(u.startswith("car-d") for u in first["updates"]))
        self.assertEqual(self.store.reputations()["car-d"], (before[0] + 1, before[1]))
        self.assertEqual(self.svc.evaluate(self.clock.t, settle=True)["updates"], [])
        self.assertEqual(self.store.reputations()["car-d"], (before[0] + 1, before[1]))

    def test_retried_report_adds_no_evidence(self):
        c = self.car("car-a")
        line = c.report(T0, LAT, LON)
        self.drive("car-a", T0 - 10)
        for _ in range(3):
            self.post(line)
            self.svc.evaluate(self.clock.t)
        hazards = self.store.nearby_hazards(LAT, LON, 100, ("unconfirmed", "suppressed"), self.clock.t)
        self.assertEqual(len(hazards), 1)
        self.assertEqual(len(self.store.hazard_evidence(hazards[0]["hazard_id"])["support"]), 1)

    def test_two_cars_reporting_one_hole_are_two_pieces_of_evidence(self):
        """Same pothole, different reports: not duplicates. The engine decides how they count."""
        self.hazard()
        hazards = self.store.nearby_hazards(LAT, LON, 100, ("corroborated", "confirmed"), self.clock.t)
        self.assertEqual(len(hazards), 1)
        self.assertEqual(hazards[0]["independent_sources"], 2)
        support = self.store.hazard_evidence(hazards[0]["hazard_id"])["support"]
        self.assertEqual({e["device_id"] for e in support}, {"car-a", "car-b"})

    def test_hazard_keeps_its_id_as_evidence_grows(self):
        self.hazard()
        first = self.store.nearby_hazards(LAT, LON, 100, ("corroborated",), self.clock.t)[0]["hazard_id"]
        c = self.car("car-c")
        self.drive("car-c", T0 + 40)
        self.post(c.report(T0 + 50, LAT, LON))
        self.clock.t = T0 + 120
        self.svc.evaluate(self.clock.t)
        self.assertEqual(self.store.nearby_hazards(LAT, LON, 100, ("corroborated", "confirmed"), self.clock.t)
                         [0]["hazard_id"], first)

    def test_same_device_cannot_farm_reputation_from_one_hazard(self):
        self.hazard(("car-a", "car-b", "car-c"))
        self.svc.settle(self.clock.t)
        before = self.store.reputations()["car-a"]
        self.assertGreater(before[0], 3.0)    # credited once, on top of the vetted start
        self.clock.t += 60
        for k in range(3):                    # the same car keeps reporting the same confirmed hole
            self.assertEqual(self.post(self.cars["car-a"].report(self.clock.t - k, LAT, LON)).status_code, 201)
        self.svc.settle(self.clock.t)
        self.assertEqual(self.store.reputations()["car-a"], before)

    def test_implausible_report_records_why_the_engine_refused_it(self):
        c = self.car("car-a")
        self.post(c.report(T0, LAT, LON, position_accuracy_m=500.0))
        self.svc.evaluate(self.clock.t)
        r = self.store.report("car-a", "car-a-1-1")
        self.assertEqual(r["evaluation_status"], "rejected_by_engine")
        reasons = r["evaluation_reasons"]
        self.assertIn("accuracy", json.dumps(reasons))

    def test_every_run_records_the_configuration_used(self):
        """NFR-08: a decision can be traced to its inputs and configuration version."""
        self.hazard()
        hz = self.store.nearby_hazards(LAT, LON, 100, ("corroborated",), self.clock.t)[0]
        self.assertTrue(hz["reasons"])
        self.assertTrue(self.store.hazard(hz["hazard_id"])["last_run_id"])
        self.assertEqual(len(self.svc.config_hash), 16)


class Ahead(Case):
    def ask(self, north: float, east: float, headers=None, heading=90.0, **body) -> dict:
        lat, lon = at(north, east)
        payload = {"lat": lat, "lon": lon, **body}
        if heading is not None:
            payload["heading_deg"] = heading
        r = self.client.post("/hazards/ahead", json=payload, headers=headers or self.driver)
        self.assertEqual(r.status_code, 200, r.text)
        return r.json()

    def route(self, *points: tuple[float, float]) -> list[dict]:
        return [dict(zip(("lat", "lon"), at(n, e))) for n, e in points]

    def setUp(self):
        super().setUp()
        self.driver = self.new_user()

    def test_hazard_ahead_on_the_road_alerts_at_the_set_distance(self):
        self.hazard()
        far = self.ask(0, -800)
        self.assertEqual(len(far["hazards"]), 1)         # found by the search, not yet worth an alert
        self.assertFalse(far["hazards"][0]["alert"])
        near = self.ask(0, -450)
        self.assertTrue(near["hazards"][0]["alert"])
        self.assertAlmostEqual(near["hazards"][0]["distance_m"], 450, delta=1)
        self.assertEqual(near["hazards"][0]["distance_basis"], "straight_line")

    def test_fast_car_is_alerted_earlier(self):
        self.hazard()
        self.assertTrue(self.ask(0, -700, speed_mps=100.0)["hazards"][0]["alert"])

    def test_alert_is_not_repeated_until_the_cooldown_passes(self):
        self.hazard()
        self.assertTrue(self.ask(0, -450)["hazards"][0]["alert"])
        for east in (-400, -300, -200):
            hazard = self.ask(0, east)["hazards"][0]
            self.assertTrue(hazard["in_alert_range"])
            self.assertFalse(hazard["alert"])
        self.assertTrue(self.ask(0, -450, headers=self.new_user())["hazards"][0]["alert"])   # another user is separate
        self.clock.t += self.svc.ahead_cfg.alert_cooldown_s + 1
        self.svc.evaluate(self.clock.t)
        self.assertTrue(self.ask(0, -300)["hazards"][0]["alert"])

    def test_hazard_behind_the_user_does_not_alert(self):
        self.hazard()
        self.assertEqual(self.ask(0, 100)["hazards"], [])

    def test_opposite_direction_does_not_alert(self):
        self.hazard()
        self.assertEqual(self.ask(0, 300, heading=270.0)["hazards"], [])

    def test_hazard_on_a_parallel_street_does_not_alert(self):
        self.hazard()
        self.assertEqual(self.ask(60, -300)["hazards"], [])

    def test_expired_hazard_does_not_alert(self):
        self.hazard()
        self.assertEqual(len(self.ask(0, -300)["hazards"]), 1)
        self.clock.t += 3 * DAY                         # past expires_at, the worker has not even run yet
        self.assertEqual(self.ask(0, -300)["hazards"], [])

    def test_hazard_just_behind_the_start_of_the_route_does_not_alert(self):
        self.hazard()
        for start in (1, 10, 20):
            route = self.route((0, start), (0, 500))
            self.assertEqual(self.ask(0, start, route=route)["hazards"], [], start)

    def test_hazard_behind_the_driver_on_a_route_already_under_way_does_not_alert(self):
        self.hazard()
        route = self.route((0, -300), (0, 500))
        self.assertEqual(self.ask(0, 10, route=route)["hazards"], [])

    def test_hazard_just_past_the_end_of_the_route_does_not_alert(self):
        self.hazard()
        route = self.route((0, -300), (0, -10))
        self.assertEqual(self.ask(0, -300, route=route)["hazards"], [])

    def test_route_passing_the_spot_again_later_still_alerts(self):
        """Behind the user on the first stretch, but the route comes back over it in the same direction."""
        self.hazard()
        route = self.route((0, 10), (0, 300), (100, 300), (100, -100), (0, -100), (0, 100))
        hazards = self.ask(0, 10, route=route)["hazards"]
        self.assertEqual(len(hazards), 1)
        self.assertGreater(hazards[0]["distance_m"], 500)

    def test_route_distance_is_along_the_road_not_straight_line(self):
        self.hazard()
        route = self.route((-100, -300), (0, -300), (0, 200))     # north to the corner, then east over the hazard
        h = self.ask(-100, -300, heading=None, route=route)["hazards"][0]
        self.assertEqual(h["distance_basis"], "route")
        self.assertAlmostEqual(h["distance_m"], 400, delta=2)     # straight line would be 316 m
        self.assertTrue(h["alert"])

    def test_route_turning_off_before_the_hazard_does_not_alert(self):
        self.hazard()
        route = self.route((0, -300), (0, -60), (300, -60))
        answer = self.ask(0, -300, route=route)
        self.assertEqual(answer["route_status"], "followed")
        self.assertEqual(answer["hazards"], [])

    def test_hazard_in_the_other_lane_of_the_route_does_not_alert(self):
        self.hazard()
        route = self.route((0, 300), (0, -300))                   # driving west over the same spot
        self.assertEqual(self.ask(0, 300, heading=270.0, route=route)["hazards"], [])

    def test_far_from_the_route_falls_back_to_the_current_heading(self):
        self.hazard()
        route = self.route((400, -500), (400, 500))
        answer = self.ask(0, -300, route=route)
        self.assertEqual(answer["route_status"], "off_route")
        self.assertEqual(answer["hazards"][0]["distance_basis"], "straight_line")

    def test_unconfirmed_hazard_is_not_an_alert(self):
        c = self.car("car-a", vetted=False)
        self.drive("car-a", T0 - 10)
        self.post(c.report(T0, LAT, LON))
        self.svc.evaluate(self.clock.t)
        self.assertEqual(self.ask(0, -300)["hazards"], [])
        advisory = self.ask(0, -300, include_advisories=True)["hazards"]
        self.assertEqual(advisory[0]["level"], "advisory")
        self.assertFalse(advisory[0]["alert"])

    def test_asking_does_not_store_the_position(self):
        self.hazard()
        beacons = self.store.count("beacons")
        self.ask(0, -300)
        self.assertEqual(self.store.count("beacons"), beacons)

    def test_direction_is_required(self):
        lat, lon = at(0, 0)
        r = self.client.post("/hazards/ahead", json={"lat": lat, "lon": lon}, headers=self.driver)
        self.assertEqual(r.status_code, 422)

    def test_get_hazards_excludes_a_parallel_street_when_heading_is_known(self):
        self.hazard()
        lat, lon = at(60, -300)
        got = self.client.get("/hazards", params={"lat": lat, "lon": lon, "heading_deg": 90}, headers=self.driver)
        self.assertEqual(got.json(), [])


class Access(Case):
    def test_personal_endpoints_need_a_valid_sign_in(self):
        lat, lon = at(0, 0)
        for method, url, kwargs in (("GET", "/me", {}), ("GET", "/hazards", {"params": {"lat": lat, "lon": lon}}),
                                    ("GET", "/reports", {"params": {"device_id": "x"}}), ("GET", "/devices/x", {}),
                                    ("POST", "/beacons", {"json": []}),
                                    ("POST", "/hazards/ahead", {"json": {"lat": lat, "lon": lon, "heading_deg": 0}})):
            self.assertEqual(self.client.request(method, url, **kwargs).status_code, 401, url)
            self.assertEqual(self.client.request(method, url, headers={"Authorization": "Bearer bad"},
                                                 **kwargs).status_code, 401, url)

    def test_server_will_not_start_open_by_accident(self):
        with mock.patch.dict("os.environ", {}, clear=True):
            with self.assertRaises(RuntimeError):
                create_app(self.store, self.clock, admin_token="x")

    def test_user_cannot_read_another_users_device_or_reports(self):
        alice, bob = self.new_user(), self.new_user()
        c = self.owned_car(alice, "alice-car")
        self.post(c.report(T0, LAT, LON))
        self.assertEqual(self.client.get("/reports/alice-car/alice-car-1-1", headers=alice).status_code, 200)
        self.assertEqual(self.client.get("/devices/alice-car", headers=alice).status_code, 200)
        for url, params in (("/reports/alice-car/alice-car-1-1", {}), ("/reports", {"device_id": "alice-car"}),
                            ("/devices/alice-car", {}), ("/devices/no-such-device", {})):
            self.assertEqual(self.client.get(url, params=params, headers=bob).status_code, 403, url)

    def test_user_cannot_send_positions_for_another_users_device(self):
        alice, bob = self.new_user(), self.new_user()
        self.owned_car(alice, "alice-car")
        lat, lon = at(0, 0)
        row = {"vehicle_id": "alice-car", "t": T0, "lat": lat, "lon": lon, "speed_mps": 10, "heading_deg": 90}
        self.assertEqual(self.client.post("/beacons", json=[row], headers=bob).status_code, 403)
        self.assertEqual(self.client.post("/beacons", json=[row], headers=alice).json(), {"stored": 1})

    def test_pairing_code_works_once_and_only_for_its_device(self):
        c = Car("car-z")
        reg = self.client.post("/devices", json={"device_id": "car-z", "public_key": c.public_hex}, headers=ADMIN)
        code = reg.json()["pairing_code"]
        alice, bob = self.new_user(), self.new_user()
        self.assertEqual(self.client.post("/me/devices", json={"device_id": "car-z", "pairing_code": "wrong"},
                                          headers=alice).status_code, 403)
        self.assertEqual(self.client.post("/me/devices", json={"device_id": "car-z", "pairing_code": code},
                                          headers=alice).status_code, 201)
        self.assertEqual(self.client.post("/me/devices", json={"device_id": "car-z", "pairing_code": code},
                                          headers=bob).status_code, 403)
        self.assertEqual(self.client.get("/me", headers=alice).json()["devices"], ["car-z"])
        self.assertEqual(self.client.get("/me", headers=bob).json()["devices"], [])

    def test_driver_cannot_use_operator_endpoints(self):
        driver = self.new_user()
        self.car("car-a")
        self.assertEqual(self.client.post("/admin/settle", headers=driver).status_code, 403)
        self.assertEqual(self.client.post("/admin/evaluate", headers=driver).status_code, 403)
        self.assertEqual(self.client.get("/admin/overview", headers=driver).status_code, 403)
        self.assertEqual(self.client.post("/devices/car-a/revoke", headers=driver).status_code, 403)
        self.assertEqual(self.client.put(f"/admin/users/{self.uid(driver)}/role", json={"role": "admin"},
                                         headers=driver).status_code, 403)

    def test_administrator_role_comes_from_the_database_and_takes_effect_at_once(self):
        driver = self.new_user()
        self.assertEqual(self.client.get("/admin/overview", headers=driver).status_code, 403)
        r = self.client.put(f"/admin/users/{self.uid(driver)}/role", json={"role": "admin"}, headers=ADMIN)
        self.assertEqual(r.status_code, 200)
        self.assertEqual(self.client.get("/admin/overview", headers=driver).status_code, 200)
        self.client.put(f"/admin/users/{self.uid(driver)}/role", json={"role": "driver"}, headers=ADMIN)
        self.assertEqual(self.client.get("/admin/overview", headers=driver).status_code, 403)

    def test_driver_sees_published_hazards_only_and_never_the_evidence(self):
        self.hazard()
        admin, driver = self.new_user("admin"), self.new_user()
        hazard_id = self.store.nearby_hazards(LAT, LON, 100, ("corroborated",), self.clock.t)[0]["hazard_id"]
        seen = self.client.get(f"/hazards/{hazard_id}", headers=driver)
        self.assertEqual(seen.status_code, 200)
        self.assertNotIn("evidence", seen.json())
        self.assertIn("evidence", self.client.get(f"/hazards/{hazard_id}", headers=admin).json())
        self.clock.t += 3 * DAY
        self.assertEqual(self.client.get(f"/hazards/{hazard_id}", headers=driver).status_code, 404)

    def test_reports_are_authenticated_by_the_device_not_a_user(self):
        """The phone that relays a report may have no sign-in at that moment; the signature proves the source."""
        c = self.car("car-a")
        self.assertEqual(self.post(c.report(T0, LAT, LON)).status_code, 201)


class OldDatabaseFile(unittest.TestCase):
    def test_file_from_before_this_schema_stops_with_a_clear_message(self):
        with tempfile.TemporaryDirectory() as d:
            path = f"{d}/old.sqlite3"
            db = sqlite3.connect(path)
            db.execute("CREATE TABLE reports (event_id TEXT PRIMARY KEY, line TEXT)")
            db.close()
            with self.assertRaisesRegex(RuntimeError, "delete it"):
                Store(path)


class Tokens(unittest.TestCase):
    URL = "https://project.supabase.co"
    SECRET = "x" * 40

    def claims(self, **changes) -> dict:
        now = time.time()
        return {"sub": "user-1", "aud": "authenticated", "iss": f"{self.URL}/auth/v1", "exp": now + 60, "iat": now,
                **changes}

    def test_valid_supabase_token_is_accepted(self):
        token = jwt.encode(self.claims(), self.SECRET, algorithm="HS256")
        self.assertEqual(SupabaseVerifier(self.URL, self.SECRET).verify(token), "user-1")

    def test_expired_wrong_audience_wrong_issuer_and_wrong_key_are_refused(self):
        v = SupabaseVerifier(self.URL, self.SECRET)
        for token in (jwt.encode(self.claims(exp=time.time() - 60), self.SECRET, algorithm="HS256"),
                      jwt.encode(self.claims(aud="anon"), self.SECRET, algorithm="HS256"),
                      jwt.encode(self.claims(iss="https://evil.example/auth/v1"), self.SECRET, algorithm="HS256"),
                      jwt.encode(self.claims(), "y" * 40, algorithm="HS256"),
                      jwt.encode(self.claims(), "", algorithm="none"),
                      "not a token"):
            with self.assertRaises(InvalidToken):
                v.verify(token)

    def test_token_without_expiry_is_refused(self):
        claims = self.claims()
        del claims["exp"]
        with self.assertRaises(InvalidToken):
            SupabaseVerifier(self.URL, self.SECRET).verify(jwt.encode(claims, self.SECRET, algorithm="HS256"))

    def test_asymmetric_project_key_works_and_a_shared_secret_token_cannot_replace_it(self):
        key = ec.generate_private_key(ec.SECP256R1())
        v = SupabaseVerifier(self.URL)
        signing = mock.Mock(key=key.public_key())
        with mock.patch.object(v.jwks, "get_signing_key_from_jwt", return_value=signing):
            self.assertEqual(v.verify(jwt.encode(self.claims(), key, algorithm="ES256")), "user-1")
            with self.assertRaises(InvalidToken):
                v.verify(jwt.encode(self.claims(), self.SECRET, algorithm="HS256"))

    def test_unreachable_key_server_is_not_reported_as_a_bad_token(self):
        v = SupabaseVerifier(self.URL)
        with mock.patch.object(v.jwks, "get_signing_key_from_jwt",
                               side_effect=jwt.PyJWKClientConnectionError("no route")):
            with self.assertRaises(AuthUnavailable):
                v.verify("whatever")

    def test_key_server_certificates_do_not_depend_on_the_python_install(self):
        context = SupabaseVerifier(self.URL).jwks.ssl_context
        self.assertTrue(context.get_ca_certs())
        self.assertEqual(context.verify_mode, ssl.CERT_REQUIRED)


class Contract(Case):
    def test_golden_device_lines_still_pass_the_whole_path(self):
        self.register("demo-car-01", json.loads(GOLDEN[0])["public_key"])
        for line in GOLDEN:
            self.assertEqual(self.post(line.encode()).json()["status"], "accepted")
        self.svc.evaluate(self.clock.t)
        self.assertEqual(self.store.pending_count(), 0)
