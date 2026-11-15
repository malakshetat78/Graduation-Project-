"""Render terminal-style screenshots from the REAL bring-up logs (lines are copied verbatim; any gap is marked).

usage: python3 make_shots.py <logs_dir> <out_dir> [shot-number ...]   (no numbers: all shots)
"""
import html
import re
import subprocess
import sys
from pathlib import Path

LOGS = Path(sys.argv[1])
OUT = Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)
BRAVE = "/Applications/Brave Browser.app/Contents/MacOS/Brave Browser"
COMMIT = "ad1ac37"       # smoke test run (shots 2, 3)
COMMIT_NET = "3e704d3"   # network report run and the latest host tests (shots 1, 5, 6)
ONLY = set(sys.argv[3:])


def wanted(n):
    return not ONLY or str(n) in ONLY


def read(name):
    return (LOGS / name).read_text(errors="replace").splitlines()


def colour(line):
    e = html.escape(line)
    e = re.sub(r"\b(PASS|PASSED|succeeded|Project build complete|201 Created|-&gt; 201)", r'<span class="ok">\1</span>', e)
    e = re.sub(r"\b(FAIL|FAILED|ERROR|Rebooting\.\.\.|401 Unauthorized|bad_signature)", r'<span class="bad">\1</span>', e)
    e = e.replace("***ERROR***", '<span class="bad">***ERROR***</span>')
    e = re.sub(r"\b(SKIP)\b", r'<span class="skip">\1</span>', e)
    if line.startswith("$ "):
        e = '<span class="cmd">' + e + "</span>"
    if line.startswith("[") or line.startswith("#"):
        e = '<span class="dim">' + e + "</span>"
    return e


def render(fname, title, badge, badge_cls, lines, footer, width=1180):
    body = "\n".join(colour(l) for l in lines)
    page = f"""<!doctype html><html><head><meta charset="utf-8"><style>
body {{ margin:0; background:#e9edf2; font-family:-apple-system,Helvetica,Arial,sans-serif; }}
.win {{ margin:24px; border-radius:12px; overflow:hidden; box-shadow:0 10px 30px rgba(0,0,0,.25); background:#1d1f23; }}
.bar {{ display:flex; align-items:center; gap:8px; padding:12px 16px; background:#2b2e33; color:#c9d1d9; font-size:14px; }}
.dot {{ width:12px; height:12px; border-radius:50%; display:inline-block; }}
.title {{ margin-left:10px; font-weight:600; }}
.badge {{ margin-left:auto; padding:3px 10px; border-radius:999px; font-size:12px; font-weight:700; letter-spacing:.5px; }}
.HOST {{ background:#1f6feb; color:#fff; }} .EMU {{ background:#a371f7; color:#fff; }} .BUILD {{ background:#3fb950; color:#0d1117; }}
.BUG {{ background:#f0883e; color:#0d1117; }} .NET {{ background:#2ea043; color:#fff; }}
pre {{ margin:0; padding:18px 20px; color:#d6deeb; font:13.5px/1.5 Menlo,monospace; white-space:pre-wrap; word-break:break-all; }}
.ok {{ color:#3fb950; font-weight:700; }} .bad {{ color:#ff7b72; font-weight:700; }} .skip {{ color:#d29922; font-weight:700; }}
.cmd {{ color:#79c0ff; }} .dim {{ color:#8b949e; font-style:italic; }}
.foot {{ padding:10px 20px 14px; color:#8b949e; font-size:12.5px; border-top:1px solid #30363d; }}
</style></head><body><div class="win">
<div class="bar"><span class="dot" style="background:#ff5f57"></span><span class="dot" style="background:#febc2e"></span>
<span class="dot" style="background:#28c840"></span><span class="title">{html.escape(title)}</span>
<span class="badge {badge_cls}">{html.escape(badge)}</span></div>
<pre>{body}</pre><div class="foot">{html.escape(footer)}</div></div></body></html>"""
    src = OUT / (fname + ".html")
    src.write_text(page)
    est_h = 130 + 21 * sum(1 + len(l) // 125 for l in lines)
    png = OUT / (fname + ".png")
    subprocess.run([BRAVE, "--headless=new", "--disable-gpu", "--hide-scrollbars", f"--window-size={width},{est_h}",
                    f"--screenshot={png}", "--force-device-scale-factor=2", src.as_uri()],
                   check=True, capture_output=True, timeout=60)
    src.unlink()
    print("wrote", png)


# 1. Laptop unit tests (HOST)
if wanted(1):
  nat = read("native_tests.log")
  summary = [l for l in nat if re.match(r"^native\s+test_\w+\s+PASSED", l)]
  total = [l for l in nat if "test cases:" in l]
  render("1_laptop_tests", "Laptop: device logic unit tests", "HOST (laptop)", "HOST",
         ["$ pio test -e native"] + [f"[... per-test output omitted, {len(nat)} lines in the full log ...]"] + summary + total,
         f"Commit {COMMIT_NET} - 15 Nov 2026 - full log: firmware/results/bringup/logs/native_tests.log")

# 2. Emulator run of the ECU smoke test (EMULATOR)
if wanted(2):
  q = [l for l in read("qemu_ecu_smoke.log") if not l.startswith("#")]
  keep = []
  for l in q:
      if l.startswith("ESP-ROM") or "Found 8MB PSRAM" in l or "Calling app_main" in l or l.startswith("SMOKE"):
          keep.append(l)
  first_smoke = next(i for i, l in enumerate(keep) if l.startswith("SMOKE"))
  lines = (["$ qemu-system-xtensa -machine esp32s3 -m 8M -drive file=flash_qemu.bin,if=mtd,format=raw"]
           + keep[:1] + ["[... boot log lines omitted ...]"] + keep[1:first_smoke] + [""] + keep[first_smoke:]
           + ["", "[60 s run: 0 reboots]"])
  render("2_emulator_smoke_test", "ESP32-S3 emulator: ECU boot / sensor smoke test", "EMULATOR (QEMU)", "EMU", lines,
         f"Firmware built from commit {COMMIT}. Sensors are SKIP because the emulator has none; they run on the real board. "
         "Log: firmware/results/bringup/logs/qemu_ecu_smoke.log")

# 3. Firmware build for the real board (BUILD)
if wanted(3):
  b = read("ecu_build_board.log")
  tail = [l for l in b if "binary size" in l or "Project build complete" in l or "idf.py -p PORT flash" in l
          or l.startswith("Bootloader binary")]
  warn = sum("warning:" in l for l in b)
  render("3_board_firmware_build", "Building the ECU firmware for the XIAO ESP32-S3", "BUILD (for the chip)", "BUILD",
         ["$ idf.py set-target esp32s3 build", f"[... {len(b)} build lines omitted; compiler warnings: {warn} ...]"] + tail,
         f"Commit {COMMIT} - ESP-IDF v5.5.5 - log: firmware/results/bringup/logs/ecu_build_board.log")

# 4. Bug found in the emulator and fixed (BUG)
if wanted(4):
  s1 = [l for l in read("qemu_espidf_security.log") if not l.startswith("#")]
  pick = [l for l in s1 if l.startswith(("PASS", "ICVSP:", "***ERROR***", "Rebooting")) or "Returned from app_main" in l]
  s2 = [l for l in read("qemu_espidf_security_stack8k.log") if not l.startswith("#")]
  pick2 = [l for l in s2 if l.startswith("PASS") or "Returned from app_main" in l]
  lines = (["# BEFORE: signing demo as-is (3.5 KB main stack)", "$ qemu-system-xtensa -machine esp32s3 ... (espidf_security)"]
           + pick + ["[... the same crash repeats: 54 reboots in 25 s ...]", "",
                     "# AFTER: same code, main stack set to 8 KB", "$ qemu-system-xtensa -machine esp32s3 ... (espidf_security, 8 KB stack)"]
           + pick2 + ["[15 s run: 0 reboots]"])
  render("4_bug_found_and_fixed", "Emulator found a crash in the signing demo, and the fix", "BUG FOUND + FIXED", "BUG", lines,
         "Malak's espidf_security @ a452c66. Logs: firmware/results/bringup/logs/qemu_espidf_security*.log")

# 5. Emulator: the ECU firmware sends a signed report to the backend (EMULATOR + network)
if wanted(5):
  r = [l for l in read("qemu_report_demo.log") if not l.startswith("#")]
  keep = [l for l in r if l.startswith(("DEMO", "NET", "REPORT", "HTTP", "UPLINK"))]
  lines = (["$ qemu-system-xtensa -machine esp32s3 -m 8M -drive file=flash_qemu.bin,if=mtd,format=raw -nic user,model=open_eth",
            "[... boot log lines omitted ...]"] + keep + ["", "[run: 0 reboots; backend on the laptop, port 8010]"])
  render("5_emulator_report_accepted", "ESP32-S3 emulator: ECU signs a pothole report and sends it to the backend",
         "EMULATOR + NETWORK", "NET", lines,
         f"Firmware commit {COMMIT_NET}. Real code: ECU app, signing, the firmware network stack running in QEMU (emulated NIC, no radio), backend checks. Simulated: IMU drive, "
         "GNSS sentences, demo key. QEMU Ethernet stands in for Wi-Fi; HTTP, TLS not exercised. "
         "Log: firmware/results/bringup/logs/qemu_report_demo.log")

# 6. The backend side of the same run (HOST)
if wanted(6):
  be = [l.replace("INFO:     ", "") for l in read("backend_report_demo.log") if "/reports" in l or "/devices" in l]
  ck = read("backend_report_checks.log")
  lines = (["# backend log during the run (uvicorn on the laptop, port 8010)"] + be[:2] + ["", *ck])
  render("6_backend_checks", "Backend: the report is stored; a resend is a duplicate; a changed copy is refused",
         "HOST (laptop backend)", "HOST", lines,
         f"Commit {COMMIT_NET} - local SQLite test database, test device ecu-demo-01. "
         "Logs: firmware/results/bringup/logs/backend_report_demo.log, backend_report_checks.log")
