# 5_TESTING/doc

Hardware test cases and the logs of every run on the physical XIAO ESP32-S3 Sense (ESP-IDF firmware).

| Part | Ticket | Owner | Test cases |
|---|---|---|---|
| Camera | T-012 | Shahesta | [camera/T-012-camera-test-cases.md](camera/T-012-camera-test-cases.md) |
| IMU | T-010 | Rana | [imu/T-010-imu-test-cases.md](imu/T-010-imu-test-cases.md) |
| GNSS | T-014 | Ahmed | [gnss/T-014-gnss-test-cases.md](gnss/T-014-gnss-test-cases.md) |
| BLE | T-016 | Mariam | [ble/T-016-ble-test-cases.md](ble/T-016-ble-test-cases.md) |
| All-in-one ECU (**not run on the board yet**) | - | Shahesta | [ecu_all_in_one/ECU-all-in-one-test-cases.md](ecu_all_in_one/ECU-all-in-one-test-cases.md) |

Each `runN_.../` folder holds the raw board log of that run. GNSS logs never contain coordinates.

[ecu_aws/](ecu_aws/README.md) keeps the earlier emulator run that sent a signed report to the AWS backend. It is
emulator evidence, not a board test.
