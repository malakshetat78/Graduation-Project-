"""Cross-language integration: compile actual ESP-IDF C signer and POST its output to real FastAPI.

Run on Windows CMD from repository's backend/:
    python -m unittest tests.test_espidf_contract -v

Requires gcc on PATH (MinGW or compatible), FastAPI, httpx and cryptography.
Uses a deliberately PUBLIC test-only seed in a temporary native executable;
never put this seed in production firmware.
"""
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from fastapi.testclient import TestClient

from icvsp_backend.app import create_app
from icvsp_backend.store import Store

ROOT = Path(__file__).resolve().parents[2]
FIRMWARE = ROOT / "firmware" / "espidf_security"
SRC = FIRMWARE / "main"
MONO = FIRMWARE / "components" / "monocypher"
DRIVER = FIRMWARE / "tools" / "signer_cli.c"
T = 1790000000.0
ADMIN = {"X-Admin-Token": "test-admin"}


class EspIdfToBackend(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        gcc = shutil.which("gcc")
        if not gcc:
            raise RuntimeError("gcc is not on PATH. Install/use MinGW GCC before running the native-C integration test.")
        cls.work = tempfile.TemporaryDirectory(prefix="icvsp-native-signer-")
        cls.binary = Path(cls.work.name) / ("signer.exe" if __import__("os").name == "nt" else "signer")
        cmd = [
            gcc, "-std=c11", "-O2",
            "-I", str(SRC), "-I", str(MONO),
            str(DRIVER), str(SRC / "report_signer.c"),
            str(MONO / "monocypher.c"), str(MONO / "monocypher-ed25519.c"),
            "-o", str(cls.binary),
        ]
        subprocess.run(cmd, check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "work"):
            cls.work.cleanup()

    def setUp(self):
        self.store = Store()
        self.client = TestClient(create_app(self.store, lambda: T + 60, admin_token="test-admin"))

    def tearDown(self):
        self.client.close()
        self.store.close()

    def signed(self, event_id="vehicle_001-1-1", sequence=1, severity="high"):
        event = {
            "schema_version": 2,
            "event_id": event_id,
            "device_id": "vehicle_001",
            "boot_id": 1,
            "sequence": sequence,
            "type": "pothole",
            "detected_at": "2026-09-21T14:13:40.000Z",
            "gps_valid": True,
            "latitude": 30.0444,
            "longitude": 31.236473,
            "position_accuracy_m": 3.5,
            "speed_kmh": 13.4,
            "heading_deg": 90.0,
            "severity": severity,
            "detection_confidence": 0.98,
            "model": "imu-v0",
        }
        payload = json.dumps(event, separators=(",", ":"), ensure_ascii=False)
        result = subprocess.run(
            [str(self.binary), "vehicle_001", event_id, "1790000020000"],
            input=payload + "\n", text=True, capture_output=True, check=True,
        )
        return result.stdout.strip().encode("utf-8")

    def register(self, signed_line):
        public_key = json.loads(signed_line)["public_key"]
        resp = self.client.post("/devices", json={
            "device_id": "vehicle_001", "public_key": public_key
        }, headers=ADMIN)
        self.assertEqual(resp.status_code, 201, resp.text)

    def send(self, line):
        return self.client.post(
            "/reports", content=line, headers={"Content-Type": "application/json"}
        )

    def test_c_generated_signature_is_accepted_and_identical_retry_is_duplicate(self):
        line = self.signed()
        self.register(line)
        first = self.send(line)
        self.assertEqual(first.status_code, 201, first.text)
        self.assertEqual(first.json()["status"], "accepted")
        retry = self.send(line)
        self.assertEqual(retry.status_code, 201, retry.text)
        self.assertEqual(retry.json()["status"], "duplicate")
        self.assertEqual(len(self.store.reports_since(0)), 1)

    def test_tampered_c_report_is_rejected(self):
        line = self.signed()
        self.register(line)
        changed = line.replace(b'"severity":"high"', b'"severity":"low"')
        self.assertNotEqual(line, changed)
        response = self.send(changed)
        self.assertEqual(response.status_code, 401, response.text)

    def test_conflicting_signed_boot_sequence_is_rejected(self):
        first = self.signed()
        self.register(first)
        self.assertEqual(self.send(first).status_code, 201)
        # Different event ID, but same boot 1 + sequence 1, valid C signature.
        other = self.signed(event_id="vehicle_001-1-1b", sequence=1)
        response = self.send(other)
        self.assertEqual(response.status_code, 409, response.text)


if __name__ == "__main__":
    unittest.main()
