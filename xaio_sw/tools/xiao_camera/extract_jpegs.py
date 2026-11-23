#!/usr/bin/env python3
"""Turn a saved T-012 camera test log into .jpg files and a results summary.

The camera bring-up firmware prints sample photos as Base64 blocks between
"-----BEGIN ICVSP JPEG ...-----" and "-----END ICVSP JPEG-----", plus CAM and CAMSTAT lines.
Save the whole Serial Monitor output to a text file, then run:

    python3 extract_jpegs.py camera_log.txt --out evidence/

It writes one .jpg per block, checks each one (declared size, JPEG start/end markers, well-formed frame and
scan headers, width and height) and, if Pillow is installed (pip install Pillow), decodes every photo fully.
It prints a Markdown summary to paste into the test results.

Exit code 0 only if the log holds exactly one complete run that passed: a CAM RESULT line with fail=0, no
CAM ... FAIL line, at least one photo, and every photo OK. Anything else exits 1 and says why (a run with the
camera unplugged, TC-CAM-10, is expected to exit 1). Without Pillow the photos are only checked structurally.
Pillow catches broken headers and tables but fills in missing image data without an error, so open the photos
and look at them in every case: that is the real image check (TC-CAM-06).
"""
from __future__ import annotations

import argparse
import base64
import binascii
import io
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

BEGIN_RE = re.compile(r"-----BEGIN ICVSP JPEG (?P<attrs>.*)-----\s*$")
END_RE = re.compile(r"-----END ICVSP JPEG-----\s*$")
ATTR_RE = re.compile(r'(\w+)=("([^"]*)"|\S+)')
CAM_RE = re.compile(r"^CAM (?P<check>\S+)\s+(?P<status>PASS|FAIL)\s+(?P<detail>.*)$")
RESULT_RE = re.compile(r"^CAM RESULT pass=(?P<passed>\d+) fail=(?P<failed>\d+)")
BEGIN_RUN_RE = re.compile(r"^CAM BEGIN ")
B64_LINE_RE = re.compile(r"^[A-Za-z0-9+/=]+$")
# The Arduino IDE Serial Monitor can add "12:34:56.789 -> " time stamps: remove them.
PREFIX_RE = re.compile(r"^\d{2}:\d{2}:\d{2}\.\d{3} -> ")


@dataclass
class Image:
    attrs: dict[str, str]
    data: bytes = b""
    problems: list[str] = field(default_factory=list)
    width: int = 0
    height: int = 0
    decoded: str = "not checked"
    path: Path | None = None


def parse_attrs(text: str) -> dict[str, str]:
    return {m.group(1): (m.group(3) if m.group(3) is not None else m.group(2)) for m in ATTR_RE.finditer(text)}


def jpeg_headers(data: bytes) -> tuple[int, int, int]:
    """Same walk as jpeg_check.cpp: (width, height, scan_offset) from well-formed SOF and SOS headers.

    width/height are 0 without a well-formed frame header, scan_offset is 0 without a well-formed scan header.
    """
    width = height = 0
    pos = 2 if data.startswith(b"\xff\xd8") else len(data)
    while pos + 4 <= len(data) and data[pos] == 0xFF:
        marker = data[pos + 1]
        if marker == 0xFF:
            pos += 1
            continue
        if marker in (0xD8, 0x01) or 0xD0 <= marker <= 0xD7:
            pos += 2
            continue
        seg_len = (data[pos + 2] << 8) | data[pos + 3]
        if seg_len < 2 or pos + 2 + seg_len > len(data):
            break
        if marker in (0xC0, 0xC1, 0xC2):
            components = data[pos + 9] if seg_len >= 8 else 0
            if width or data[pos + 4] != 8 or not 1 <= components <= 4 or seg_len != 8 + 3 * components:
                return 0, 0, 0
            height = (data[pos + 5] << 8) | data[pos + 6]
            width = (data[pos + 7] << 8) | data[pos + 8]
        elif marker == 0xDA:
            components = data[pos + 4] if seg_len >= 3 else 0
            if width and 1 <= components <= 4 and seg_len == 6 + 2 * components:
                return width, height, pos + 2 + seg_len
            break
        pos += 2 + seg_len
    return width, height, 0


def jpeg_size(data: bytes) -> tuple[int, int]:
    width, height, _ = jpeg_headers(data)
    return width, height


def decode_with_pillow(data: bytes) -> str | None:
    """'WxH' if Pillow decodes the whole picture, raises ValueError if it cannot, None if Pillow is missing."""
    try:
        from PIL import Image as PILImage
    except ImportError:
        return None
    try:
        with PILImage.open(io.BytesIO(data)) as picture:
            picture.load()  # decodes all image data; a truncated or corrupt stream raises
            return f"{picture.width}x{picture.height}"
    except Exception as exc:  # Pillow raises several types for bad data
        raise ValueError(str(exc) or type(exc).__name__) from exc


# Tests replace this to check the structural part without Pillow.
DECODER = decode_with_pillow


def check_image(img: Image) -> None:
    data = img.data
    declared = img.attrs.get("bytes")
    if declared is not None and declared.isdigit() and int(declared) != len(data):
        img.problems.append(f"decoded {len(data)} bytes, log said {declared}")
    if not data.startswith(b"\xff\xd8"):
        img.problems.append("no JPEG start marker FF D8")
    end = data.rfind(b"\xff\xd9", max(0, len(data) - 64))
    if end < 0:
        img.problems.append("no JPEG end marker FF D9 near the end")
    img.width, img.height, scan = jpeg_headers(data)
    if img.width == 0:
        img.problems.append("no well-formed frame header with width and height")
    elif scan == 0:
        img.problems.append("no well-formed scan header (SOS) after the frame header")
    elif end >= 0 and end <= scan:
        img.problems.append("no image data between the scan header and FF D9")
    for key, actual in (("width", img.width), ("height", img.height)):
        want = img.attrs.get(key)
        if want is not None and want.isdigit() and int(want) != actual:
            img.problems.append(f"{key} {actual} in the JPEG, log said {want}")
    if img.problems or DECODER is None:
        return
    try:
        size = DECODER(data)
    except ValueError as exc:
        img.decoded = "FAILED"
        img.problems.append(f"Pillow could not decode the picture: {exc}")
        return
    if size is None:
        img.decoded = "not checked (install Pillow)"
    elif size != f"{img.width}x{img.height}":
        img.decoded = size
        img.problems.append(f"Pillow decoded {size}, header said {img.width}x{img.height}")
    else:
        img.decoded = "yes"


def parse_log(lines: list[str]) -> tuple[list[Image], list[str], list[dict[str, str]], list[tuple[str, str, str]], str]:
    images: list[Image] = []
    errors: list[str] = []
    stats: list[dict[str, str]] = []
    checks: list[tuple[str, str, str]] = []
    result = ""
    runs = 0
    current: Image | None = None
    chunks: list[str] = []
    for number, raw in enumerate(lines, 1):
        line = PREFIX_RE.sub("", raw.rstrip("\r\n"))
        begin = BEGIN_RE.search(line)
        if begin:
            if current is not None:
                errors.append(f"line {number}: new image started before the previous one ended")
            current = Image(parse_attrs(begin.group("attrs")))
            chunks = []
            continue
        if END_RE.search(line):
            if current is None:
                errors.append(f"line {number}: END without BEGIN")
                continue
            try:
                current.data = base64.b64decode("".join(chunks), validate=True)
            except (binascii.Error, ValueError) as exc:
                current.problems.append(f"Base64 could not be decoded: {exc}")
            check_image(current)
            images.append(current)
            current = None
            continue
        if current is not None:
            if B64_LINE_RE.match(line):
                chunks.append(line)
            elif line.strip():
                current.problems.append(f"line {number}: unexpected text inside the image block")
            continue
        if line.startswith("CAMSTAT "):
            stats.append(parse_attrs(line[len("CAMSTAT "):]))
        elif BEGIN_RUN_RE.match(line):
            runs += 1
            if runs == 2:
                errors.append(f"line {number}: a second test run starts here: save one run per log file")
        elif RESULT_RE.match(line):
            result = line.strip()
        else:
            cam = CAM_RE.match(line)
            if cam:
                checks.append((cam.group("check"), cam.group("status"), cam.group("detail").strip()))
    if current is not None:
        errors.append("log ended inside an image block (copy the whole output)")
    return images, errors, stats, checks, result


def run_problems(images: list[Image], checks: list[tuple[str, str, str]], result: str) -> list[str]:
    """Reasons the log is not one complete, passing run with photos (empty list = it is)."""
    problems = []
    match = RESULT_RE.match(result)
    if not match:
        problems.append("no CAM RESULT line: the run did not finish or the log was cut")
    elif int(match.group("failed")) != 0:
        problems.append(f"the firmware reported {match.group('failed')} failed check(s)")
    failed = [name for name, status, _ in checks if status == "FAIL"]
    if failed:
        problems.append("FAIL lines: " + ", ".join(failed))
    if match and int(match.group("passed")) + int(match.group("failed")) != len(checks):
        problems.append(f"CAM RESULT counts {int(match.group('passed')) + int(match.group('failed'))} checks but "
                        f"the log has {len(checks)} CAM check lines (lines missing from the log?)")
    if not images:
        problems.append("no photos in the log (set CONFIG_ICVSP_CAM_DUMP_JPEG to 1)")
    return problems


def safe_name(text: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]", "_", text) or "image"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", type=Path, help="saved Serial Monitor output")
    parser.add_argument("--out", type=Path, default=Path("camera_evidence"), help="folder for the .jpg files")
    args = parser.parse_args(argv)

    lines = args.log.read_text(encoding="utf-8", errors="replace").splitlines()
    images, errors, stats, checks, result = parse_log(lines)
    args.out.mkdir(parents=True, exist_ok=True)
    for index, img in enumerate(images, 1):
        if img.data:
            img.path = args.out / f"{index:02d}_{safe_name(img.attrs.get('name', 'image'))}.jpg"
            img.path.write_bytes(img.data)

    print(f"## Camera test log: {args.log.name}\n")
    print(f"Result line: `{result or 'not found (did the test finish?)'}`\n")
    if checks:
        print("| Check | Status | Details |\n| --- | --- | --- |")
        for name, status, detail in checks:
            print(f"| {name} | {status} | {detail.replace('|', '/')} |")
        print()
    res_stats = [s for s in stats if "res" in s]
    if res_stats:
        print("| Resolution | Valid / shots | Init ms | Wait ms min/avg/max | FPS | Bytes min/avg/max | Lighting |")
        print("| --- | --- | --- | --- | --- | --- | --- |")
        for s in res_stats:
            print(f"| {s.get('res')} {s.get('width')}x{s.get('height')} | {s.get('valid')}/{s.get('shots')} | "
                  f"{s.get('init_ms')} | {s.get('get_ms_min')}/{s.get('get_ms_avg')}/{s.get('get_ms_max')} | "
                  f"{s.get('fps')} | {s.get('bytes_min')}/{s.get('bytes_avg')}/{s.get('bytes_max')} | "
                  f"{s.get('lighting')} |")
        print()
    mem_stats = [s for s in stats if "when" in s]
    if mem_stats:
        print("| Memory snapshot | Internal free | Internal largest block | PSRAM free | Lowest free ever |")
        print("| --- | --- | --- | --- | --- |")
        for s in mem_stats:
            print(f"| {s.get('when')} | {s.get('internal_free', 'not measured')} | "
                  f"{s.get('internal_largest', 'not measured')} | {s.get('psram_free', 'not measured')} | "
                  f"{s.get('heap_min_ever', 'not measured')} |")
        print()
    build = next((s for s in stats if "firmware" in s), None)
    if build:
        print(f"Firmware `{build.get('firmware')}`, ESP-IDF `{build.get('idf')}`, built {build.get('compiled')}\n")
    print("| Image file | Size | Bytes | Decoded | Check |\n| --- | --- | --- | --- | --- |")
    for img in images:
        status = "OK" if not img.problems else "PROBLEM: " + "; ".join(img.problems)
        name = img.path.name if img.path else img.attrs.get("name", "?")
        print(f"| {name} | {img.width}x{img.height} | {len(img.data)} | {img.decoded} | {status} |")
    if not images:
        print("| (no images in the log) | | | | |")
    for err in errors:
        print(f"\nLOG PROBLEM: {err}")
    run = run_problems(images, checks, result)
    for problem in run:
        print(f"\nRUN PROBLEM: {problem}")

    bad = errors or run or any(img.problems for img in images)
    print(f"\nWrote {sum(1 for i in images if i.path)} image(s) to {args.out}/. Open them and check they show the scene.")
    print("Verdict: " + ("NOT a complete passing run (see the problems above)" if bad else
                         "one complete run, all checks passed, all photos OK"))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
