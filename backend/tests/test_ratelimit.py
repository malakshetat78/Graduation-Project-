"""Rate limit tests (FR-SEC-20): who is counted, what a refused request gets (429 with Retry-After), and that normal use
is left alone. Time is a fake clock, so nothing waits. Run:  cd backend && python3 -m unittest -v"""
import json
import logging
import unittest
import uuid
from unittest import mock

from fastapi.testclient import TestClient
from starlette.requests import Request

from icvsp_backend.app import create_app
from icvsp_backend.ratelimit import DEFAULTS, Limiter, RateLimits

from tests.test_api import ADMIN, T0, Clock
from tests.test_backend import LAT, LON, Case, FakeVerifier, at

JSON = {"Content-Type": "application/json"}


def request(peer="10.0.0.1", forwarded=None) -> Request:
    headers = [(b"x-forwarded-for", forwarded.encode())] if forwarded is not None else []
    return Request({"type": "http", "headers": headers, "client": (peer, 5000), "method": "GET", "path": "/"})


class Window(unittest.TestCase):
    def test_blocks_after_the_limit_and_allows_again_after_the_window(self):
        t = Clock(100.0)
        lim = Limiter(3, 60, t)
        self.assertTrue(all(lim.hit("a").allowed for _ in range(3)))
        d = lim.hit("a")
        self.assertFalse(d.allowed)
        self.assertEqual(d.retry_after, 60)
        t.t += 59
        self.assertEqual(lim.hit("a").retry_after, 1)
        t.t += 1
        self.assertTrue(lim.hit("a").allowed)

    def test_keys_are_counted_separately(self):
        lim = Limiter(1, 60, Clock(0.0))
        self.assertTrue(lim.hit("a").allowed)
        self.assertFalse(lim.hit("a").allowed)
        self.assertTrue(lim.hit("b").allowed)

    def test_refused_requests_are_not_counted_so_waiting_is_enough(self):
        t = Clock(0.0)
        lim = Limiter(2, 60, t)
        lim.hit("a"); lim.hit("a")
        for _ in range(500):
            lim.hit("a")
        t.t = 60
        self.assertTrue(lim.hit("a").allowed)

    def test_memory_stays_bounded_with_many_keys(self):
        lim = Limiter(5, 60, Clock(0.0), max_keys=100)
        for i in range(10_000):
            lim.hit(f"ip-{i}")
        self.assertLessEqual(len(lim._hits), 100)


class ClientIp(unittest.TestCase):
    def test_forwarded_header_is_ignored_unless_trusted(self):
        rl = RateLimits(trust_proxy=False)
        self.assertEqual(rl.client_ip(request("10.0.0.1", "203.0.113.9")), "10.0.0.1")

    def test_trusted_header_uses_the_entry_the_proxy_added_not_the_one_the_client_wrote(self):
        rl = RateLimits(trust_proxy=True)
        self.assertEqual(rl.client_ip(request("10.0.0.1", "6.6.6.6, 203.0.113.9")), "203.0.113.9")
        self.assertEqual(RateLimits(trust_proxy=True, proxy_hops=2).client_ip(
            request("10.0.0.1", "6.6.6.6, 203.0.113.9, 10.1.1.1")), "203.0.113.9")

    def test_without_a_usable_header_the_connection_address_is_used(self):
        rl = RateLimits(trust_proxy=True)
        self.assertEqual(rl.client_ip(request("127.0.0.1")), "127.0.0.1")                  # laptop demo, no proxy
        self.assertEqual(rl.client_ip(request("127.0.0.1", "not-an-ip")), "127.0.0.1")
        self.assertEqual(rl.client_ip(request("127.0.0.1", "")), "127.0.0.1")
        self.assertEqual(RateLimits(trust_proxy=True, proxy_hops=2).client_ip(
            request("127.0.0.1", "203.0.113.9")), "127.0.0.1")


class LimitCase(Case):
    def setUp(self):
        super().setUp()
        self.rt = Clock(1000.0)

    def limited(self, trust_proxy=False, **rules) -> RateLimits:
        """The same app with small limits (rule=(requests, seconds)) and a clock of its own."""
        rl = RateLimits(trust_proxy=trust_proxy, rules=rules, clock=self.rt)
        self.client = TestClient(create_app(self.store, self.clock, admin_token="test-admin", rate_limits=rl,
                                            verifier=FakeVerifier(), allow_anonymous=False))
        self.svc = self.client.app.state.service
        self.svc.eval_on_read = False
        return rl

    def junk(self, headers=None):
        return self.client.post("/reports", content=b"junk", headers={**JSON, **(headers or {})})


class PublicLimits(LimitCase):
    def test_reports_over_the_limit_get_429_with_retry_after_and_cost_nothing(self):
        self.limited(reports=(3, 60))
        self.assertEqual([self.junk().status_code for _ in range(3)], [400, 400, 400])
        rows = self.store.count("rejections")
        r = self.junk()
        self.assertEqual(r.status_code, 429)
        self.assertEqual(r.headers["Retry-After"], "60")
        self.assertEqual(r.json(), {"detail": "too many requests, retry later", "code": "rate_limited", "retryable": True})
        self.assertEqual(self.store.count("rejections"), rows)       # a refused request is not even logged in the database

    def test_the_same_report_is_accepted_when_the_phone_retries_after_the_wait(self):
        c = self.car("car-a")
        line = c.report(T0 + 30, LAT, LON)
        self.limited(reports=(1, 60))
        self.junk()
        self.assertEqual(self.client.post("/reports", content=line, headers=JSON).status_code, 429)
        self.rt.t += 60
        self.assertEqual(self.client.post("/reports", content=line, headers=JSON).status_code, 201)

    def test_limit_is_per_ip_when_proxy_headers_are_trusted(self):
        self.limited(trust_proxy=True, reports=(2, 60))
        a, b = {"X-Forwarded-For": "203.0.113.1"}, {"X-Forwarded-For": "203.0.113.2"}
        self.junk(a); self.junk(a)
        self.assertEqual(self.junk(a).status_code, 429)
        self.assertEqual(self.junk(b).status_code, 400)

    def test_an_invented_forwarded_header_does_not_help_when_proxy_headers_are_not_trusted(self):
        self.limited(trust_proxy=False, reports=(2, 60))
        codes = [self.junk({"X-Forwarded-For": f"198.51.100.{i}"}).status_code for i in range(4)]
        self.assertEqual(codes, [400, 400, 429, 429])

    def test_an_invented_leftmost_entry_does_not_help_when_proxy_headers_are_trusted(self):
        self.limited(trust_proxy=True, reports=(2, 60))
        codes = [self.junk({"X-Forwarded-For": f"198.51.100.{i}, 203.0.113.7"}).status_code for i in range(4)]
        self.assertEqual(codes, [400, 400, 429, 429])

    def test_forged_reports_in_a_device_name_do_not_use_up_that_devices_limit(self):
        car = self.car("car-a")
        self.limited(trust_proxy=True, reports=(3, 60))
        forged = car.report(T0 + 30, LAT, LON).replace(b'"signature":"', b'"signature":"00')
        attacker = {"X-Forwarded-For": "198.51.100.66"}
        for _ in range(5):
            self.client.post("/reports", content=forged, headers={**JSON, **attacker})
        own = self.client.post("/reports", content=car.report(T0 + 31, LAT, LON),
                               headers={**JSON, "X-Forwarded-For": "203.0.113.5"})
        self.assertEqual(own.status_code, 201)

    def test_health_ready_and_the_pages_are_limited_each_on_its_own(self):
        self.limited(health=(2, 60), ready=(1, 60), pages=(1, 60))
        self.assertEqual([self.client.get("/health").status_code for _ in range(3)], [200, 200, 429])
        self.assertEqual([self.client.get("/ready").status_code for _ in range(2)], [200, 429])
        for page in ("/dashboard", "/docs", "/openapi.json"):
            self.limited(pages=(1, 60))
            self.assertEqual([self.client.get(page).status_code for _ in range(2)], [200, 429], page)


class UserLimits(LimitCase):
    def test_limit_is_per_user_and_not_per_ip(self):
        self.limited(user=(3, 60))
        alice, bob = self.new_user(), self.new_user()
        self.assertEqual([self.client.get("/me", headers=alice).status_code for _ in range(4)], [200, 200, 200, 429])
        self.assertEqual(self.client.get("/me", headers=bob).status_code, 200)     # same IP, other user

    def test_a_refused_user_request_says_when_to_retry(self):
        self.limited(user=(1, 30))
        alice = self.new_user()
        self.client.get("/me", headers=alice)
        r = self.client.get("/me", headers=alice)
        self.assertEqual((r.status_code, r.headers["Retry-After"], r.json()["retryable"]), (429, "30", True))
        self.rt.t += 30
        self.assertEqual(self.client.get("/me", headers=alice).status_code, 200)

    def test_pairing_code_guessing_is_limited_per_user(self):
        self.limited(claim=(3, 60))
        c = self.car("car-z", vetted=False)
        code = self.client.post("/devices/car-z/pairing-code", headers=ADMIN).json()["pairing_code"]
        alice = self.new_user()
        guess = {"device_id": "car-z", "pairing_code": "wrong"}
        self.assertEqual([self.client.post("/me/devices", json=guess, headers=alice).status_code for _ in range(4)],
                         [403, 403, 403, 429])
        right = {"device_id": "car-z", "pairing_code": code}
        self.assertEqual(self.client.post("/me/devices", json=right, headers=alice).status_code, 429)
        self.rt.t += 60
        self.assertEqual(self.client.post("/me/devices", json=right, headers=alice).status_code, 201)

    def test_a_signed_in_admin_is_counted_like_any_user(self):
        self.limited(user=(2, 60))
        admin = self.new_user("admin")
        self.assertEqual([self.client.get("/admin/overview", headers=admin).status_code for _ in range(3)],
                         [200, 200, 429])


class FailedSignIns(LimitCase):
    def test_guessing_the_admin_token_is_stopped_but_the_right_token_works_from_elsewhere(self):
        self.limited(trust_proxy=True, bad_admin_token=(3, 60))
        bad = {"X-Admin-Token": "guess", "X-Forwarded-For": "198.51.100.66"}
        self.assertEqual([self.client.get("/admin/overview", headers=bad).status_code for _ in range(5)],
                         [401, 401, 401, 429, 429])
        right_here = {**ADMIN, "X-Forwarded-For": "198.51.100.66"}
        self.assertEqual(self.client.get("/admin/overview", headers=right_here).status_code, 429)   # not even compared
        right_elsewhere = {**ADMIN, "X-Forwarded-For": "203.0.113.5"}
        self.assertEqual(self.client.get("/admin/overview", headers=right_elsewhere).status_code, 200)

    def test_a_signed_in_admin_still_works_while_the_ip_is_guessing_the_token(self):
        self.limited(trust_proxy=True, bad_admin_token=(1, 60))
        admin = {**self.new_user("admin"), "X-Forwarded-For": "198.51.100.66"}
        self.client.get("/admin/overview", headers={"X-Admin-Token": "guess", "X-Forwarded-For": "198.51.100.66"})
        self.assertEqual(self.client.get("/admin/overview", headers=admin).status_code, 200)

    def test_a_flood_of_invalid_tokens_becomes_429_but_a_valid_token_is_never_refused(self):
        self.limited(bad_token=(3, 60))
        bad = {"Authorization": "Bearer bad"}
        self.assertEqual([self.client.get("/me", headers=bad).status_code for _ in range(5)], [401, 401, 401, 429, 429])
        self.assertEqual(self.client.get("/me", headers=self.new_user()).status_code, 200)

    def test_requests_without_any_token_are_not_counted_as_failures(self):
        self.limited(bad_token=(1, 60), bad_admin_token=(1, 60))
        self.assertEqual([self.client.get("/me").status_code for _ in range(5)], [401] * 5)
        self.assertEqual([self.client.get("/admin/overview").status_code for _ in range(5)], [401] * 5)


class Settings(LimitCase):
    def test_limits_can_be_switched_off(self):
        rl = self.limited(reports=(1, 60))
        rl.enabled = False
        self.assertEqual([self.junk().status_code for _ in range(5)], [400] * 5)
        with mock.patch.dict("os.environ", {"ICVSP_RATE_LIMIT_ENABLED": "0"}, clear=True):
            self.assertFalse(RateLimits.from_env().enabled)

    def test_development_mode_without_sign_in_has_no_limits_unless_asked(self):
        with mock.patch.dict("os.environ", {}, clear=True):
            c = TestClient(create_app(self.store, self.clock, admin_token="x", allow_anonymous=True))
            self.assertEqual([c.post("/reports", content=b"junk").status_code for _ in range(DEFAULTS["reports"][0] + 5)]
                             .count(429), 0)
        with mock.patch.dict("os.environ", {"ICVSP_RATE_LIMIT_ENABLED": "1", "ICVSP_RATE_REPORTS": "2/60"}, clear=True):
            c = TestClient(create_app(self.store, self.clock, admin_token="x", allow_anonymous=True))
            self.assertEqual([c.post("/reports", content=b"junk").status_code for _ in range(3)], [400, 400, 429])

    def test_numbers_and_proxy_setting_come_from_the_environment(self):
        env = {"ICVSP_RATE_REPORTS": "7/10", "ICVSP_RATE_USER": "9/5", "ICVSP_TRUST_PROXY_HEADERS": "1",
               "ICVSP_TRUSTED_PROXY_HOPS": "2"}
        with mock.patch.dict("os.environ", env, clear=True):
            rl = RateLimits.from_env()
        self.assertEqual((rl.rules["reports"], rl.rules["user"], rl.rules["claim"]), ((7, 10), (9, 5), DEFAULTS["claim"]))
        self.assertEqual((rl.trust_proxy, rl.proxy_hops, rl.enabled), (True, 2, True))
        with mock.patch.dict("os.environ", {}, clear=True):
            self.assertFalse(RateLimits.from_env().trust_proxy)       # closed by default

    def test_a_wrong_setting_stops_the_server_at_start(self):
        for env in ({"ICVSP_RATE_REPORTS": "lots"}, {"ICVSP_RATE_REPORTS": "0/60"}, {"ICVSP_TRUSTED_PROXY_HOPS": "x"}):
            with mock.patch.dict("os.environ", env, clear=True), self.assertRaises(RuntimeError):
                RateLimits.from_env()

    def test_normal_use_of_one_phone_stays_under_the_default_limits(self):
        rl = RateLimits(clock=self.rt)
        self.client = TestClient(create_app(self.store, self.clock, admin_token="test-admin", rate_limits=rl,
                                            verifier=FakeVerifier(), allow_anonymous=False))
        alice = self.new_user()
        self.owned_car(alice, "alice-car")
        lat, lon = at(0, 0)
        row = {"vehicle_id": "alice-car", "t": T0, "lat": lat, "lon": lon, "speed_mps": 10, "heading_deg": 90}
        ahead = {"lat": lat, "lon": lon, "heading_deg": 90}
        for second in range(60):          # a minute of driving: a position and a look ahead every second
            self.rt.t += 1
            self.assertEqual(self.client.post("/beacons", json=[row], headers=alice).status_code, 200)
            self.assertEqual(self.client.post("/hazards/ahead", json=ahead, headers=alice).status_code, 200)
        self.assertEqual(self.client.get("/health").status_code, 200)

    def test_a_refusal_is_logged_once_per_window_without_tokens_or_secrets(self):
        self.limited(user=(1, 60), bad_token=(1, 60), bad_admin_token=(1, 60))
        alice = self.new_user()
        with self.assertLogs("icvsp.security", level="WARNING") as cm:
            for _ in range(6):
                self.client.get("/me", headers=alice)
            for _ in range(3):
                self.client.get("/me", headers={"Authorization": "Bearer bad"})
            for _ in range(3):
                self.client.get("/admin/overview", headers={"X-Admin-Token": "wrong-admin-guess-123"})
        lines = [l for l in cm.output if "rate_limited" in l]
        self.assertEqual(sorted(l.split("rule=")[1].split()[0] for l in lines), ["bad_admin_token", "bad_token", "user"])
        text = "\n".join(lines)                                         # one line per rule and key, not per request
        self.assertIn("retry_after=", text)
        for secret in ("wrong-admin-guess-123", "test-admin", "Bearer"):
            self.assertNotIn(secret, text)


if __name__ == "__main__":
    unittest.main()
