# Screenshots

Rendered by `make_shots.py` from the committed logs in `../logs/`: every line shown is copied from a real log;
lines in grey italics are notes (omitted lines, run length), not device output. The badge says where it ran.

| Image | Where | What it shows |
|---|---|---|
| `1_laptop_tests.png` | HOST (laptop) | 106/106 unit tests of the device logic |
| `2_emulator_smoke_test.png` | EMULATOR (QEMU) | The ECU firmware boots; chip and signing PASS; sensors SKIP (not emulated) |
| `3_board_firmware_build.png` | BUILD | The ECU firmware builds for the XIAO ESP32-S3 with 0 warnings |
| `4_bug_found_and_fixed.png` | EMULATOR (QEMU) | Stack overflow + reboot loop in the signing demo, and the run with the fix |
| `5_emulator_report_accepted.png` | EMULATOR + NETWORK | The ECU firmware signs a pothole report and the laptop backend stores it (201) |
| `6_backend_checks.png` | HOST (laptop backend) | Unregistered key answered 403 (the ECU retried), registration, then the stored report; the same line again is a duplicate; a changed copy is refused (bad signature). Checks made with `../backend_report_checks.py` |
| `7_emulator_unknown_device_kept.png` | EMULATOR + NETWORK | Before the key is registered: 403 unknown_device, the ECU keeps the report and retries (nothing deleted) |
| `8_https_tls13_checks.png` | EMULATOR + TLS | 6 HTTPS cases: unknown CA, wrong host, expired refused; valid certificate over TLS 1.3 (403, then 201); a TLS 1.2-only server refused |
| `9_ecu_to_aws_cloud.png` | EMULATOR + AWS CLOUD | The ECU firmware sends a signed report over the internet to the live AWS backend: 201 stored |
| `10_aws_backend_test_cases.png` | AWS CLOUD | Nourseen's 17 live test cases on the same AWS backend (T-004, PR 18), all PASS |
| `11_aws_report_evaluated.png` | AWS CLOUD | The ECU's report on the AWS side: CloudWatch `POST /reports 201`, then the trust engine's decision (evaluated, suppressed: one unvetted source) |

## Real AWS screenshots (`aws_console/`)

Taken in the browser on 16 Nov 2026 (about 22:30 local time, UTC+2), from the live AWS account and the backend's own
web dashboard, after the ECU run of screenshot 9. Not rendered from logs: these are the real pages.

| Image | Page | What it shows |
|---|---|---|
| `aws_console/12_dashboard_ecu_hazard.jpg` | Backend dashboard on AWS (`<Lambda URL>/dashboard`, admin token entered by Shahesta) | The ECU's hazard `hz_0002`: reported by `ecu-qemu-cloud-01`, state **Suppressed** (no warning, logged for review) and the trust engine's reasons |
| `aws_console/13_dashboard_live_events.jpg` | same dashboard, further down | Live events: `ecu-qemu-cloud-01 pothole - accepted` at 9:50:41 PM local (19:50:41 UTC), then Nourseen's T-004 checks; devices and reliability |
| `aws_console/14_lambda_function.jpg` | AWS Lambda console | The deployed backend function `icvsp-backend-BackendFunction-…`, its Scheduler trigger (the trust engine, every minute) and its public Function URL |
| `aws_console/15_cloudwatch_post_reports_201.jpg` | AWS CloudWatch Logs console | The backend's log at 19:50:42 UTC: `POST /reports 201`, the ECU's report arriving, then the scheduled evaluator runs |

Regenerate: `python3 make_shots.py "$PWD/../logs" "$PWD" [numbers]` (needs a Chromium browser; path set in the
script). Shots 1-3 and 5-7 come from the runs at `b89fa8f`; shot 4 from Malak's `a452c66`; shots 8-9 from `75f5f8d`; shot 11 from that run plus Nourseen's CloudWatch and operator-view exports in `../logs/aws_*`; shot 10 from
Nourseen's `cbc9ddb` (read with `git show`, so that commit must be fetched). The test cases are listed in `../TEST_CASES.md`.
