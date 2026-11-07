"""Findings of the PR 8 review: anonymous mode must not open a server that has user sign-in, a signed-in user without
the role gets 403 (not 401, which makes the app sign the user out), and failed sign-ins are recorded without the
token (FR-SEC-01, FR-SEC-02, FR-SEC-19, FR-SEC-21). Run:  cd backend && python3 -m unittest -v"""
import unittest
from unittest import mock

from fastapi.testclient import TestClient

from icvsp_backend.app import create_app

from tests.test_api import ADMIN
from tests.test_backend import LAT, LON, Case, FakeVerifier


class AnonymousMode(Case):
    def test_anonymous_switch_together_with_sign_in_stops_the_server_at_start(self):
        with self.assertRaises(RuntimeError):
            create_app(self.store, self.clock, admin_token="x", verifier=FakeVerifier(), allow_anonymous=True)
        with mock.patch.dict("os.environ", {"SUPABASE_URL": "https://x.supabase.co", "ICVSP_ALLOW_ANONYMOUS": "1"},
                             clear=True), self.assertRaises(RuntimeError):
            create_app(self.store, self.clock, admin_token="x")

    def test_with_sign_in_and_no_switch_a_request_without_a_token_is_refused(self):
        for method, url, kw in (("GET", "/me", {}), ("GET", "/hazards", {"params": {"lat": LAT, "lon": LON}}),
                                ("GET", "/devices/x", {}), ("GET", "/reports", {"params": {"device_id": "x"}}),
                                ("POST", "/beacons", {"json": []})):
            self.assertEqual(self.client.request(method, url, **kw).status_code, 401, url)

    def test_development_run_without_sign_in_still_works(self):
        c = TestClient(create_app(self.store, self.clock, admin_token="x", allow_anonymous=True))
        self.assertEqual(c.get("/hazards", params={"lat": LAT, "lon": LON}).status_code, 200)


class WrongRole(Case):
    def test_signed_in_user_without_the_role_gets_403_and_without_credentials_401(self):
        driver, ambulance = self.new_user(), self.new_user("ambulance")
        for headers in (driver, ambulance):
            self.assertEqual(self.client.get("/admin/overview", headers=headers).status_code, 403)
        self.assertEqual(self.client.get("/admin/overview").status_code, 401)
        self.assertEqual(self.client.get("/admin/overview", headers={"X-Admin-Token": "wrong"}).status_code, 401)
        self.assertEqual(self.client.get("/admin/overview", headers={"Authorization": "Bearer bad"}).status_code, 401)
        self.assertEqual(self.client.get("/admin/overview", headers=ADMIN).status_code, 200)
        self.assertEqual(self.client.get("/admin/overview", headers=self.new_user("admin")).status_code, 200)

    def test_a_wrong_admin_token_does_not_hide_a_valid_administrator_sign_in(self):
        h = {**self.new_user("admin"), "X-Admin-Token": "wrong"}
        self.assertEqual(self.client.get("/admin/overview", headers=h).status_code, 200)


class FailedSignInLog(Case):
    def lines(self, calls):
        with self.assertLogs("icvsp.security", level="WARNING") as cm:
            calls()
        return [l for l in cm.output if "auth_failure" in l]

    def test_failed_sign_ins_are_logged_with_time_and_reason_but_never_the_token(self):
        driver = self.new_user()
        lines = self.lines(lambda: (
            self.client.get("/me"),
            self.client.get("/me", headers={"Authorization": "Basic abc123"}),
            self.client.get("/me", headers={"Authorization": "Bearer bad"}),
            self.client.get("/admin/overview", headers={"X-Admin-Token": "wrong-admin-guess"}),
            self.client.get("/admin/overview", headers=driver),
            self.client.get("/me", headers={"Authorization": "Bearer 11111111-1111-4111-8111-111111111111"})))
        text = "\n".join(lines)
        for reason in ("missing credentials", "malformed credentials", "invalid or expired token",
                       "invalid admin token", "role not allowed", "no profile for this account"):
            self.assertIn(f"reason={reason}", text)
        self.assertIn("ts=", text)
        self.assertIn("path=/admin/overview", text)
        for secret in ("abc123", "wrong-admin-guess", self.uid(driver), "test-admin"):
            self.assertNotIn(secret, text)

    def test_successful_sign_ins_are_not_logged_as_failures(self):
        with self.assertNoLogs("icvsp.security", level="WARNING"):
            self.assertEqual(self.client.get("/me", headers=self.new_user()).status_code, 200)
            self.assertEqual(self.client.get("/admin/overview", headers=ADMIN).status_code, 200)


if __name__ == "__main__":
    unittest.main()
