# ECU end-to-end test cases

Each test case: what is done (input), what must happen (expected), what happened (actual), and where the proof is.
Screenshots are in `screenshots/`, rendered line by line from the real logs in `logs/`. Requirement IDs are from
`verification/requirements.csv`; the full requirement-to-test table is in `BRINGUP_CHECKLIST.md`.

Path of a report, end to end: **ECU firmware** (signs it) -> **HTTPS, TLS 1.3** -> **backend** (checks the device,
signature and freshness, stores it in the database) -> **trust engine** (evaluates it). Tested on the laptop first,
then against the live **AWS cloud** backend (team Supabase database). On AWS, the ECU's own report is proven up to
"stored" (TC-15); the trust engine step on AWS is proven with Nourseen's test report (TC-16, B8), and for the ECU's
report it is still to be recorded (TC-17).

Where: HOST = laptop, EMULATOR = the ECU firmware in the ESP32-S3 emulator (QEMU), AWS = the live backend on AWS
Lambda, BOARD = the real XIAO ESP32-S3 (not delivered yet).

## Device and firmware

| ID | Where | Input | Expected | Actual | Result | Evidence |
|---|---|---|---|---|---|---|
| TC-01 | HOST | `pio test -e native`: device logic (detection, GNSS, signing, delivery queue, Wi-Fi recovery) | all unit tests pass | 106/106 | PASS | `screenshots/1_laptop_tests.png`, `logs/native_tests.log` |
| TC-02 | BUILD | build the ECU firmware for the XIAO ESP32-S3 | builds, 0 warnings | builds, 0 warnings | PASS | `screenshots/3_board_firmware_build.png` |
| TC-03 | EMULATOR | boot the smoke-test firmware | boots, chip and signing checks pass, no reboot | PASS, 0 reboots in 60 s (sensors SKIP: not emulated) | PASS | `screenshots/2_emulator_smoke_test.png` |
| TC-04 | EMULATOR | signing demo, original stack size | runs without crashing | crashed (stack overflow, 54 reboots); fixed with an 8 KB stack, then 0 reboots | PASS after fix | `screenshots/4_bug_found_and_fixed.png` |

## ECU to backend on the laptop

| ID | Where | Input | Expected | Actual | Result | Evidence |
|---|---|---|---|---|---|---|
| TC-05 | EMULATOR + HOST | ECU sends a signed report, device key not registered | `403 unknown_device`, report kept and retried | 403, `kept=1` | PASS | `screenshots/7_emulator_unknown_device_kept.png` |
| TC-06 | EMULATOR + HOST | same chip after its key is registered | `201` stored | 201 accepted | PASS | `screenshots/5_emulator_report_accepted.png` |
| TC-07 | HOST | the same report sent again | answered as duplicate, not stored twice | `duplicate` | PASS | `screenshots/6_backend_checks.png` |
| TC-08 | HOST | a copy of the report with one field changed | refused: signature does not verify | `401 bad_signature` | PASS | `screenshots/6_backend_checks.png` |

## HTTPS (certificates and TLS version)

| ID | Where | Input | Expected | Actual | Result | Evidence |
|---|---|---|---|---|---|---|
| TC-09 | EMULATOR + HOST | server certificate from an unknown CA | TLS refused, report kept, nothing sent | refused (`-0x2700`), kept | PASS | `screenshots/8_https_tls13_checks.png`, `logs/tls/` |
| TC-10 | EMULATOR + HOST | certificate for another host name | the same | refused, kept | PASS | same |
| TC-11 | EMULATOR + HOST | expired certificate | the same | refused, kept | PASS | same |
| TC-12 | EMULATOR + HOST | valid certificate, device not registered | TLS 1.3 accepted, backend answers 403 | 403 | PASS | same |
| TC-13 | EMULATOR + HOST | valid certificate, device registered | TLS 1.3 accepted, 201 stored | 201 | PASS | same |
| TC-14 | EMULATOR + HOST | server that allows only TLS 1.2 | refused (no fallback to TLS 1.2), report kept | refused, kept | PASS | same |

## ECU to the AWS cloud

| ID | Where | Input | Expected | Actual | Result | Evidence |
|---|---|---|---|---|---|---|
| TC-15 | EMULATOR + AWS | ECU firmware (`sdkconfig.report_aws`) sends a signed report to the live Lambda URL over the internet; device `ecu-qemu-cloud-01` registered on the live backend | HTTPS with the public root certificates, TLS 1.3, `201` stored in the team database | 201 accepted, event `ecu-qemu-cloud-01-1794858622-1`, 16 Nov 2026 19:50:41 UTC | PASS | `screenshots/9_ecu_to_aws_cloud.png`, `logs/qemu_aws_report.log` |
| TC-16 | AWS | live backend test cases A1-A6, B1-B11 (health, database, admin and sign-in checks, valid / duplicate / stale / bad signature / tampered / unregistered / revoked reports, trust engine run) | each answer as expected | 17/17 | PASS | `screenshots/10_aws_backend_test_cases.png`; T-004 (Nourseen): branch `nourseen/aws-lambda-deploy` (PR 18), commit `cbc9ddb`, folder `deploy/aws/results/` |
| TC-17 | AWS | the ECU's report from TC-15 (`ecu-qemu-cloud-01-1794858622-1`) after the 1-minute trust engine run | the backend logs show it received and evaluated; the operator view shows `evaluation: evaluated` and a hazard decision | not recorded yet (the 201 answer says `evaluation: pending`, as expected at that moment) | PENDING | CloudWatch lines and operator view for this event, to be saved by Nourseen |

## Not run yet (needs the board)

Wi-Fi on the real radio, the real camera, IMU and GNSS, signing time on the chip (NFR-SEC-05), flash and NVS
encryption (SEC-EXT-E). See `BRINGUP_CHECKLIST.md` sections 5-7.
