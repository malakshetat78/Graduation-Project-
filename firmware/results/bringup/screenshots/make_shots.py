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
COMMIT = "b89fa8f"       # smoke test run (shots 2, 3)
COMMIT_NET = "b89fa8f"   # network report runs and the latest host tests (shots 1, 5, 6, 7)
COMMIT_CLOUD = "75f5f8d" # TLS 1.3 checks and the run against the AWS backend (shots 8, 9)
T004 = "cbc9ddb"         # Nourseen's T-004 live tests on AWS (shot 10), branch nourseen/aws-lambda-deploy (PR 18)
ONLY = set(sys.argv[3:])


def wanted(n):
    return not ONLY or str(n) in ONLY


def read(name):
    return (LOGS / name).read_text(errors="replace").splitlines()


def colour(line):
    e = html.escape(line)
    e = re.sub(r"\b(PASS|PASSED|succeeded|Project build complete|201 Created|-&gt; 201|/reports 201|TLSv1\.3|evaluation: evaluated)", r'<span class="ok">\1</span>', e)
    e = re.sub(r"\b(FAIL|FAILED|ERROR|Rebooting\.\.\.|401 Unauthorized|403 Forbidden|bad_signature)", r'<span class="bad">\1</span>', e)
    e = re.sub(r"(\(device key not registered: report kept, will retry\)|kept=1)", r'<span class="skip">\1</span>', e)
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
.AWS {{ background:#ff9900; color:#0d1117; }} .TLS {{ background:#8957e5; color:#fff; }}
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
         f"Commit {COMMIT_NET} - 16 Nov 2026 (UTC) - full log: firmware/results/bringup/logs/native_tests.log")

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
         "GNSS sentences. The chip's own key (made at first boot, loaded from flash). QEMU Ethernet stands in for Wi-Fi; "
         "HTTP in this run, HTTPS certificate checks in logs/tls/. "
         "Log: firmware/results/bringup/logs/qemu_report_demo.log")

# 6. The backend side of the same run (HOST)
if wanted(6):
  be = [l.replace("INFO:     ", "") for l in read("backend_report_demo.log") if "/reports" in l or "/devices" in l]
  ck = read("backend_report_checks.log")
  lines = (["# backend log: run 1 (key not registered yet), registration, run 2 (uvicorn on the laptop, port 8010)"]
           + be[:be.index(next(l for l in be if "/devices" in l)) + 2] + ["", *ck])
  render("6_backend_checks", "Backend: the report is stored; a resend is a duplicate; a changed copy is refused",
         "HOST (laptop backend)", "HOST", lines,
         f"Commit {COMMIT_NET} - local SQLite test database, test device ecu-000000000000 (emulator blank MAC). "
         "Logs: firmware/results/bringup/logs/backend_report_demo.log, backend_report_checks.log")

# 7. Emulator: device not registered yet -> 403 unknown_device, the report is KEPT and retried (EMULATOR + network)
if wanted(7):
  r = [l for l in read("qemu_report_demo_unregistered.log") if not l.startswith("#")]
  keep = [l for l in r if l.startswith(("DEMO", "NET", "REPORT", "HTTP", "UPLINK")) and not l.startswith("DEMO drive t=")]
  lines = (["# run 1 of 2: the backend does not know this device's public key yet",
            "$ qemu-system-xtensa -machine esp32s3 -m 8M -drive file=flash_qemu.bin,if=mtd,format=raw -nic user,model=open_eth",
            "[... boot log lines and drive progress lines omitted ...]"] + keep
           + ["", "[the report stays queued: it is sent again after registration, or dropped later as 422 stale]"])
  render("7_emulator_unknown_device_kept", "ESP32-S3 emulator: key not registered yet - 403, report kept and retried",
         "EMULATOR + NETWORK", "NET", lines,
         f"Firmware commit {COMMIT_NET}. Before the fix a 403 deleted the report; now 403 unknown_device keeps it "
         "(other 403s, e.g. revoked device, still drop it). Log: firmware/results/bringup/logs/qemu_report_demo_unregistered.log")


def git_show(rev, path):
    return subprocess.run(["git", "show", f"{rev}:{path}"], check=True, capture_output=True, text=True).stdout


# 8. HTTPS checks in the emulator, TLS 1.3 only (EMULATOR + TLS)
if wanted(8):
  sm = [l for l in read("tls/summary.txt") if l.strip()]
  lines = ["$ firmware/results/bringup/tls/run_tls_checks.sh logs/tls",
           "# the same emulated chip against a laptop HTTPS server with a different certificate in each case"] + sm
  render("8_https_tls13_checks", "ESP32-S3 emulator: HTTPS certificate checks, TLS 1.3 only (6 cases)",
         "EMULATOR + TLS", "TLS", lines,
         f"Firmware commit {COMMIT_CLOUD}. Cases 1-3: bad certificates refused, report kept. 4-5: good certificate, "
         "TLS 1.3, 403 then 201. 6: server allows only TLS 1.2, refused (no fallback). Logs: firmware/results/bringup/logs/tls/")

# 9. The ECU firmware sends a signed report to the live AWS backend (EMULATOR + AWS)
if wanted(9):
  r = [l for l in read("qemu_aws_report.log") if not l.startswith("#")]
  keep = [l for l in r if l.startswith(("DEMO", "NET", "REPORT", "HTTP", "UPLINK"))]
  lines = (["$ qemu-system-xtensa -machine esp32s3 -m 8M -drive file=flash.bin,if=mtd,format=raw -nic user,model=open_eth",
            "[... boot log lines omitted ...]"] + keep
           + ["", "[HTTPS over the internet to AWS Lambda (us-east-1), TLS 1.3, server certificate checked against the public root bundle]"])
  render("9_ecu_to_aws_cloud", "ESP32-S3 emulator -> AWS cloud: the ECU's signed report is stored by the live backend",
         "EMULATOR + AWS CLOUD", "AWS", lines,
         f"Firmware commit {COMMIT_CLOUD} (sdkconfig.report_aws). Test device ecu-qemu-cloud-01, registered on the live backend "
         "by Nourseen; key made on the chip. Simulated: IMU drive and GNSS at the team test location 23.5, 27.0 (Western Desert). "
         "Database: team Supabase. Log: firmware/results/bringup/logs/qemu_aws_report.log")

# 10. Nourseen's live test cases against the same AWS backend (AWS)
if wanted(10):
  import json
  checks = []
  for f in ("deploy/aws/results/t004-partA-20261116T183530Z.json", "deploy/aws/results/t004-partB-20261116T191409Z.json",
            "deploy/aws/results/t004-partB11-20261116T193030Z.json"):
      checks += json.loads(git_show(T004, f))["checks"]
  lines = ["$ python deploy/aws/integration_test.py --url https://<id>.lambda-url.us-east-1.on.aws --part A|B"]
  for c in checks:
      exp = "/".join(map(str, c["expected"]))
      ok = "PASS" if c["passed"] else "FAIL"
      code = re.search(r'"code":"(\w+)"', c.get("response") or "")
      lines.append(f"{ok}  {c['check']}: {c['request']} -> expected {exp}, got {c['status']}"
                   + (f" {code.group(1)}" if code else ""))
  render("10_aws_backend_test_cases", "AWS cloud backend: live endpoint and security test cases (T-004)",
         "AWS CLOUD", "AWS", lines,
         f"Nourseen's T-004 run on the live Lambda URL, commit {T004} (PR 18). Test device t004-test-01, revoked at the end. "
         "Evidence: deploy/aws/results/*.json, CloudWatch logs in deploy/aws/results/cloudwatch-part*.txt")

# 11. The same report on the AWS side: CloudWatch, then the trust engine's decision (AWS)
if wanted(11):
  import json
  ecu = [l for l in read("qemu_aws_report.log") if l.startswith(("DEMO device=", "HTTP POST"))]
  cw = [l for l in read("aws_cloudwatch_ecu.txt") if l[:2].isdigit()]
  ov = json.loads((LOGS / "aws_overview_ecu.json").read_text())
  r, h = ov["report"][0], ov["hazard_decisions"][0]
  lines = (["# 1. the ECU (firmware in the emulator) sends the signed report", *ecu, "",
            "# 2. AWS CloudWatch: the backend receives it, then the trust engine runs every minute", *cw, "",
            f"# 3. operator view (GET /admin/overview, {ov['snapshot_utc']}): the report and the trust engine's decision",
            f"report  {r['event_id']}  {r['type']}  at {r['latitude']}, {r['longitude']}  received {r['received_at_utc']}",
            f"        evaluation: {r['evaluation']}",
            f"hazard  {h['hazard_id']}  state: {h['state']}  ({h['action']})  severity {h['severity']}  sources: {h['independent_sources']}",
            *[f"        reason: {x}" for x in h["reasons"]]])
  render("11_aws_report_evaluated", "AWS cloud: the ECU's report received, stored and evaluated by the trust engine",
         "AWS CLOUD", "AWS", lines,
         "One report end to end: ECU firmware 75f5f8d (emulator) -> AWS Lambda -> team Supabase -> trust engine. "
         "A single unvetted source is not enough for a driver warning, so the hazard is suppressed (logged for review), as designed. "
         "Logs: logs/qemu_aws_report.log, logs/aws_cloudwatch_ecu.txt and logs/aws_overview_ecu.json (both saved by Nourseen).")
