# T-010 IMU bring-up: test cases and results

- Ticket: T-010 MPU6050 IMU bring-up on the XIAO ESP32-S3 Sense (development).
- Procedure: [Arduino sketch guide, IMU test section](../../../xaio_sw/arduino/ICVSP_ECU/START_HERE.md#imu-test-t-010-rana-no-wi-fi-needed)
- Firmware: [imu_app.cpp](../../../xaio_sw/arduino/ICVSP_ECU/src/app/imu_app.cpp), selected with `#define ICVSP_APP ICVSP_APP_IMU` in [config.h](../../../xaio_sw/arduino/ICVSP_ECU/config.h)
- Out of scope: bump/pothole detection from the IMU (that is the device logic in `xaio_sw/lib/icvsp_core`, tested on a
  laptop), and mounting the board in a car.

Every result below states its setup: **physical board**, **emulator** or **host**. Only physical-board results
count as IMU evidence.

## Test record

| Field | Run 1 (old code) | Run 2 (fixed code) |
| --- | --- | --- |
| Date | 2026-11-24 | 2026-11-24 |
| Tester | Shahesta | Shahesta |
| Setup | physical board | physical board |
| Board | XIAO ESP32-S3 Sense | XIAO ESP32-S3 Sense |
| IMU module | "MPU6050" breakout, WHO_AM_I=0x70 (MPU6500 clone), address 0x68 | same |
| Wiring | SDA D4 (GPIO5), SCL D5 (GPIO6), VCC 3V3, GND, breadboard jumpers | same |
| Ranges | +-2 g, +-250 deg/s | same |
| Arduino-ESP32 core | 3.3.12 | 3.3.12 |
| Firmware | first version of `imu_app.cpp` (empty I2C probe) | fixed `imu_app.cpp` (no empty probe, I2C restart on retry, all-zero reading = lost). The "keep the last bias" change came after run 2 |
| Log file | [imu_run1_old_code_crash.txt](run1/imu_run1_old_code_crash.txt) | [imu_run2_fixed_code.txt](run2/imu_run2_fixed_code.txt) |

Both logs are unedited Serial Monitor output. Run 2 lines 1-9 are the end of an earlier run, before the new upload.

## Test cases

| ID | Check | Steps (START_HERE.md) | Expected | Actual (run 2 unless said) | Pass/Fail | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| TC-IMU-01 | Sensor found on boot | Upload, Serial Monitor 115200, RESET | `IMU found at 0x68, WHO_AM_I=..`, then `IMU DETECTED` | `IMU found at 0x68, WHO_AM_I=0x70 MPU6500 (clone)`, `IMU DETECTED` on both boots | Pass | run2 lines 11-17, 235-244 |
| TC-IMU-02 | Gyro calibration when still | Board still on the table at RESET | `IMU gyroscope bias ...` with spread <= 2.0 deg/s | bias x=-2.01 y=2.00 z=-0.24 deg/s, spread 1.5; second boot x=-1.84 y=1.97 z=-0.21, spread 1.7 | Pass | run2 lines 16, 243 |
| TC-IMU-03 | Calibration refuses a moving board | Move the board during calibration | `calibration rejected: the sensor moved`, retries up to 5 times | 3 rejections (spread 5.2, 10.4, 5.8), 4th try accepted when still | Pass | run2 lines 240-243 |
| TC-IMU-04 | One reading every 200 ms | Leave it running | `IMU #n ax ay az |a| gx gy gz` about every 200 ms, numbering continuous | about 198 ms apart, numbering continuous | Pass | run2 from line 20 |
| TC-IMU-05 | Still board reads 1 g, gyro near 0 | Board still | `|a|` 0.95-1.05 g; gx, gy, gz within +-1 deg/s after calibration | `|a|` 0.96-1.04 g; gyro within about +-1 deg/s after calibration | Pass | run2 lines 20-32, 1459-1625 |
| TC-IMU-06 | Six sides | Hold each side still 3 s | One axis about +-1 g, the others about 0 | +Z az=+1.05, -Z az=-0.97, +X ax=+1.01, -X ax=-1.00 (hand-held), +Y ay=+0.97, -Y ay=-0.98 | Pass | run2 lines 195-208, 1459-1625, 2239-2268, 1336-1339, 1-9, 2186-2227 |
| TC-IMU-07 | Pull SDA: no crash | Pull SDA while running | `IMU LOST: I2C read failed ...`, no `Guru Meditation` | Run 1 (old code): `Guru Meditation Error ... StoreProhibited` 2 s after `IMU LOST`, board rebooted. Run 2: 3 pulls, 3 `IMU LOST`, no crash | Fail (run 1), Pass (run 2) | run1 lines 242-261; run2 lines 490, 1388, 2314 |
| TC-IMU-08 | Push SDA back: recovery | Push SDA back in | `IMU BACK`, reading numbers carry on | 3 of 3: BACK after 9 s, 7 s, 9 s; #243->#244, #1131->#1132, #2047->#2048 | Pass | run2 lines 490-500, 1388-1398, 2314-2325 |
| TC-IMU-09 | Moving after reconnect keeps the last bias | Pull and push SDA while holding the board, keep moving | `calibration skipped ... keeping the last bias x=.. y=.. z=..` | Not run with the final code. Run 2 (before the change) printed `gyro values are uncorrected` and read about -2.3/+2.3 deg/s still | Pending | — |
| TC-IMU-10 | No sensor at boot | SDA off, RESET | `IMU NOT FOUND: no I2C answer at 0x68/0x69 ...` every second, no crash; `IMU DETECTED` after plugging SDA in | Not run yet | Pending | — |

## Problems found and fixed

1. **Crash when SDA is pulled (run 1).** After `IMU LOST` the old code probed the bus with an empty I2C write. With
   SDA unplugged that crashes the Arduino-ESP32 3.3 I2C driver (`StoreProhibited` in the I2C interrupt). Fix: read
   WHO_AM_I instead of the empty probe, and restart the I2C driver before each retry. Run 2: 3 pulls, no crash.
2. **All-zero readings.** A loose SDA can read as all zeros instead of an I2C error. Fix: a reading with
   `|a| < 0.2 g` counts as lost (gravity alone is 1 g).
3. **Gyro offset after a reconnect while moving.** Calibration was skipped and the bias set to zero, so a still board
   read about 2 deg/s. Fix: keep the last good bias (same sensor). To check in TC-IMU-09.

## Next steps

1. Upload the final code and run TC-IMU-09 and TC-IMU-10. Save the log in `run3/`.
2. Then T-010 can be marked done.
