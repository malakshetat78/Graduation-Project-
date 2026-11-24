#!/usr/bin/env python3
"""Host tests for extract_jpegs.py. Run: python3 -m unittest test_extract_jpegs.py -v"""
from __future__ import annotations

import base64
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import extract_jpegs  # noqa: E402


def fake_jpeg(width: int, height: int, padding: int = 0) -> bytes:
    """Same structure as the C++ test: FF D8, APP0, SOF0 with the size, SOS, image bytes, FF D9 (not decodable)."""
    return (b"\xff\xd8"
            + b"\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00"
            + b"\xff\xc0\x00\x11\x08" + height.to_bytes(2, "big") + width.to_bytes(2, "big")
            + b"\x03\x01\x22\x00\x02\x11\x01\x03\x11\x01"
            + b"\xff\xda\x00\x0c\x03\x01\x00\x02\x11\x03\x11\x00\x3f\x00" + b"\x12\x34\x56"
            + b"\xff\xd9" + b"\x00" * padding)


def real_jpeg(width: int, height: int) -> bytes | None:
    """A decodable JPEG made with Pillow, or None if Pillow is not installed."""
    try:
        from PIL import Image as PILImage
    except ImportError:
        return None
    out = io.BytesIO()
    PILImage.new("RGB", (width, height), (90, 140, 200)).save(out, "JPEG", quality=80)
    return out.getvalue()


def block(name: str, data: bytes, width: int, height: int, declared: int | None = None) -> list[str]:
    text = base64.b64encode(data).decode()
    lines = [f'-----BEGIN ICVSP JPEG name={name} width={width} height={height} '
             f'bytes={declared if declared is not None else len(data)} lighting="bright room"-----']
    lines += [text[i:i + 76] for i in range(0, len(text), 76)]
    lines.append("-----END ICVSP JPEG-----")
    return lines


class ParseLogTest(unittest.TestCase):
    """Structure checks. The fake JPEGs are not decodable, so the Pillow decode is switched off here."""

    def setUp(self):
        self._decoder = extract_jpegs.DECODER
        extract_jpegs.DECODER = None

    def tearDown(self):
        extract_jpegs.DECODER = self._decoder

    def test_short_frame_header_is_rejected(self):
        # Codex review probe: SOI, a 7-byte SOF, zeros, EOI.
        data = b"\xff\xd8\xff\xc0\x00\x07\x08\x00\xf0\x01\x40" + b"\x00" * 500 + b"\xff\xd9"
        images, _, _, _, _ = extract_jpegs.parse_log(block("s", data, 320, 240))
        self.assertTrue(any("frame header" in p for p in images[0].problems))

    def test_missing_scan_header_is_rejected(self):
        data = bytearray(fake_jpeg(320, 240))
        data[40] = 0xFE  # SOS -> COM
        images, _, _, _, _ = extract_jpegs.parse_log(block("s", bytes(data), 320, 240))
        self.assertTrue(any("scan header" in p for p in images[0].problems))

    def test_no_image_data_is_rejected(self):
        data = fake_jpeg(320, 240)[:53] + b"\xff\xd9"
        images, _, _, _, _ = extract_jpegs.parse_log(block("s", data, 320, 240))
        self.assertTrue(any("no image data" in p for p in images[0].problems))

    def test_good_image_and_stats(self):
        jpeg = fake_jpeg(320, 240) * 1  # small but complete
        lines = (["CAM BEGIN T-012 camera bring-up test",
                  'CAMSTAT build firmware=v1-3-gabc idf=v6.1.1 compiled="Nov 21 2026 10:00:00" shots=20 lighting="bright room"',
                  "CAM startup        PASS  init 312.4 ms (attempt 1)",
                  'CAMSTAT res=QVGA width=320 height=240 shots=20 valid=20 no_frame=0 init_ms=301.2 get_ms_avg=40.1 fps=24.9 lighting="bright room"',
                  "CAMSTAT mem when=before_camera internal_free=300000 internal_largest=200000 psram_free=8000000 heap_min_ever=290000"]
                 + block("QVGA_shot01", jpeg, 320, 240)
                 + ["CAM RESULT pass=9 fail=0"])
        images, errors, stats, checks, result = extract_jpegs.parse_log(lines)
        self.assertEqual(errors, [])
        self.assertEqual(len(images), 1)
        self.assertEqual(images[0].problems, [])
        self.assertEqual((images[0].width, images[0].height), (320, 240))
        self.assertEqual(images[0].data, jpeg)
        self.assertEqual(result, "CAM RESULT pass=9 fail=0")
        self.assertEqual(checks[0][:2], ("startup", "PASS"))
        self.assertEqual(stats[0]["firmware"], "v1-3-gabc")
        self.assertEqual(stats[0]["compiled"], "Nov 21 2026 10:00:00")
        self.assertEqual(stats[1]["valid"], "20")
        self.assertEqual(stats[1]["lighting"], "bright room")
        self.assertEqual(stats[2]["when"], "before_camera")

    def test_padding_after_end_marker_is_ok(self):
        images, _, _, _, _ = extract_jpegs.parse_log(block("p", fake_jpeg(640, 480, padding=10), 640, 480))
        self.assertEqual(images[0].problems, [])

    def test_wrong_declared_length(self):
        jpeg = fake_jpeg(320, 240)
        images, _, _, _, _ = extract_jpegs.parse_log(block("x", jpeg, 320, 240, declared=len(jpeg) + 5))
        self.assertTrue(any("log said" in p for p in images[0].problems))

    def test_wrong_size_in_header(self):
        images, _, _, _, _ = extract_jpegs.parse_log(block("x", fake_jpeg(320, 240), 640, 480))
        self.assertTrue(any("width 320" in p for p in images[0].problems))

    def test_truncated_jpeg(self):
        jpeg = fake_jpeg(320, 240)[:-2]
        images, _, _, _, _ = extract_jpegs.parse_log(block("t", jpeg, 320, 240))
        self.assertTrue(any("end marker" in p for p in images[0].problems))

    def test_log_cut_inside_block(self):
        lines = block("c", fake_jpeg(320, 240), 320, 240)[:-1]
        images, errors, _, _, _ = extract_jpegs.parse_log(lines)
        self.assertEqual(images, [])
        self.assertTrue(errors)

    def test_driver_message_inside_block_is_reported(self):
        lines = block("d", fake_jpeg(320, 240), 320, 240)
        lines.insert(2, "W (1234) cam_hal: FB-SIZE: 100 != 200")
        images, _, _, _, _ = extract_jpegs.parse_log(lines)
        self.assertTrue(any("unexpected text" in p for p in images[0].problems))

    def test_arduino_timestamps_are_removed(self):
        lines = ["10:00:00.123 -> " + line for line in block("s", fake_jpeg(320, 240), 320, 240)]
        images, errors, _, _, _ = extract_jpegs.parse_log(lines)
        self.assertEqual(errors, [])
        self.assertEqual(images[0].problems, [])

    def test_main_writes_files_and_exit_code(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "log.txt"
            log.write_text("\n".join(block("QVGA_shot01", fake_jpeg(320, 240), 320, 240)
                                     + ["CAM startup        PASS  ok", "CAM RESULT pass=1 fail=0"]), encoding="utf-8")
            out = Path(tmp) / "out"
            with redirect_stdout(io.StringIO()) as printed:
                code = extract_jpegs.main([str(log), "--out", str(out)])
            self.assertEqual(code, 0)
            self.assertEqual([p.name for p in out.iterdir()], ["01_QVGA_shot01.jpg"])
            self.assertIn("| 01_QVGA_shot01.jpg | 320x240 |", printed.getvalue())

            log.write_text("\n".join(block("bad", fake_jpeg(320, 240)[:-2], 320, 240)), encoding="utf-8")
            with redirect_stdout(io.StringIO()):
                self.assertEqual(extract_jpegs.main([str(log), "--out", str(out)]), 1)

    def run_main(self, lines: list[str]) -> tuple[int, str]:
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / "log.txt"
            log.write_text("\n".join(lines), encoding="utf-8")
            with redirect_stdout(io.StringIO()) as printed:
                code = extract_jpegs.main([str(log), "--out", str(Path(tmp) / "out")])
            return code, printed.getvalue()

    def test_run_without_result_line_fails(self):
        code, text = self.run_main(block("q", fake_jpeg(320, 240), 320, 240))
        self.assertEqual(code, 1)
        self.assertIn("no CAM RESULT line", text)

    def test_failed_run_fails(self):
        code, text = self.run_main(["CAM startup        FAIL  camera did not start", "CAM RESULT pass=0 fail=1"])
        self.assertEqual(code, 1)
        self.assertIn("FAIL lines: startup", text)
        self.assertIn("no photos", text)

    def test_missing_check_lines_fail(self):
        code, text = self.run_main(block("q", fake_jpeg(320, 240), 320, 240)
                                   + ["CAM startup        PASS  ok", "CAM RESULT pass=13 fail=0"])
        self.assertEqual(code, 1)
        self.assertIn("13 checks but the log has 1", text)

    def test_two_runs_in_one_log_fail(self):
        run = ["CAM BEGIN T-012 camera bring-up test"] + block("q", fake_jpeg(320, 240), 320, 240) + [
            "CAM startup        PASS  ok", "CAM RESULT pass=1 fail=0"]
        code, text = self.run_main(run + run)
        self.assertEqual(code, 1)
        self.assertIn("second test run", text)

    def test_memory_not_measured_is_shown(self):
        code, text = self.run_main(block("q", fake_jpeg(320, 240), 320, 240) + [
            "CAMSTAT mem when=running_QVGA not_measured=1", "CAM startup        PASS  ok", "CAM RESULT pass=1 fail=0"])
        self.assertEqual(code, 0)
        self.assertIn("| running_QVGA | not measured |", text)


@unittest.skipIf(real_jpeg(8, 8) is None, "Pillow not installed: photos are only checked structurally")
class DecodeTest(unittest.TestCase):
    def test_real_photo_decodes(self):
        images, _, _, _, _ = extract_jpegs.parse_log(block("r", real_jpeg(320, 240), 320, 240))
        self.assertEqual(images[0].problems, [])
        self.assertEqual(images[0].decoded, "yes")

    def test_structurally_fine_but_undecodable_is_rejected(self):
        images, _, _, _, _ = extract_jpegs.parse_log(block("f", fake_jpeg(320, 240), 320, 240))
        self.assertEqual(images[0].decoded, "FAILED")
        self.assertTrue(any("Pillow could not decode" in p for p in images[0].problems))



if __name__ == "__main__":
    unittest.main()
