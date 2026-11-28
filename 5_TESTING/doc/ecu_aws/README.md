# ECU firmware to the AWS backend (emulator run, kept as evidence)

Before the board arrived, the ECU report firmware was run in the ESP32-S3 emulator (QEMU) and sent a signed pothole
report over HTTPS (TLS 1.3) to the live backend on AWS Lambda with the team Supabase database. **This is emulator
evidence, not a hardware test**: the board tests are in the other folders of `5_TESTING/doc`. The emulator code
itself was removed; the same report code now runs on the board as the `report` firmware in `1_XAIO_SW/code`.

| File | What it shows |
|---|---|
| `screenshots/9_ecu_to_aws_cloud.png` | the firmware's signed report reaching the AWS backend: `201` stored (drawn from the run's log) |
| `screenshots/10_aws_backend_test_cases.png` | Nourseen's 17 live backend test cases on the same AWS backend (T-004), all pass (drawn from the log) |
| `screenshots/11_aws_report_evaluated.png` | the same report on the AWS side: CloudWatch `POST /reports 201`, then the trust engine's decision (drawn from the logs) |
| `screenshots/aws_console/12-15` | real screenshots of the AWS dashboard, the Lambda function and CloudWatch after that run |
| `logs/aws_*` | CloudWatch log and operator-view export of that run |
| `logs/backend_report_*`, `backend_report_checks.py` | the earlier laptop run: the backend stores the emulator's report, accepts a repeat as a duplicate and refuses a changed one |
