# T-010 IMU bring-up (ESP-IDF): test cases and results

- Ticket: T-010 IMU bring-up (MPU6050 on I2C). Original test code by Rana, moved to ESP-IDF.
- Firmware: [components/icvsp_imu](../../../1_XAIO_SW/code/components/icvsp_imu) in the ESP-IDF project
  `1_XAIO_SW/code`, started by [main/imu_main.cpp](../../../1_XAIO_SW/code/main/imu_main.cpp). Selected with the
  build preset **T-010 IMU test** (`sdkconfig.imu`).
- Wiring: SDA GPIO5, SCL GPIO6, I2C port 1 at 400 kHz (port 0 is the camera). Range +-2 g and +-250 deg/s.
- Out of scope: crash and pothole detection. These tests only show that the sensor is found, reads sensible
  values, and that the firmware survives a lost connection.

Every result states its setup. All results below are from the **physical board**.

## How to run it

1. VS Code with the ESP-IDF extension (ESP-IDF v5.5.x), open `1_XAIO_SW/code`.
2. `Cmd/Ctrl+Shift+P` > **ESP-IDF: Select Project Configuration** > **T-010 IMU test**.
3. Put the board flat on the table and do not touch it for the first 3 s after it starts (gyro calibration).
4. Click **Build, Flash and Monitor** (flame icon).
5. Wait for `IMU DETECTED`, then one `IMU #n` line every 200 ms. Save everything the monitor printed to
   `imu_log_raw.txt`.

Command line instead of VS Code:

```bash
. ~/esp/esp-idf/export.sh
cd 1_XAIO_SW/code
idf.py -B build_imu -D SDKCONFIG=build_imu/sdkconfig -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.imu" set-target esp32s3 build
idf.py -B build_imu -p PORT flash monitor
```

What the lines mean:

| Line | Meaning |
| --- | --- |
| `IMU found at 0x68, WHO_AM_I=0x70 MPU6500 (clone)` | Sensor answered. Our board has a clone that reports 0x70 instead of 0x68 |
| `IMU gyroscope bias x=.. y=.. z=.. deg/s (spread ..)` | Gyro calibration done. Spread must be 2.0 deg/s or less |
| `IMU calibration rejected: the sensor moved` | Board moved during calibration; it tries again (5 tries) |
| `IMU DETECTED` | Sensor ready, readings start |
| `IMU #n t=.. ms ax ay az g \|a\| g gx gy gz deg/s` | One reading. `\|a\|` is the total acceleration, 1 g when still |
| `IMU LOST: I2C read failed` / `IMU BACK` | Connection dropped / came back. It retries every second |

## Test record

All runs: 2026-11-24, tester Shahesta, **physical board** XIAO ESP32-S3 Sense, IMU module on GPIO5/GPIO6
(wires soldered), ESP-IDF v5.5.5, same board and USB cable. `firmware=16ad91a-dirty` in every log: the code was
not committed yet, so the version below says which code each run used.

| Run | Board / purpose | Firmware version | Folder |
| --- | --- | --- | --- |
| 1 | moved during start, then flat upside down | A: first ESP-IDF port | [run1_moving_at_start](run1_moving_at_start) |
| 2 | held in the hand and shaken, then on its side | A | [run2_gyro_noise](run2_gyro_noise) |
| 3 | flat and still | A | [run3_filter_off_still](run3_filter_off_still) |
| 4 | still, then the six sides, then hard movement | B: gyro filter on | [run4_filter_on](run4_filter_on) |
| 5 | flat and still, about 22 s | B | [run5_still_timed](run5_still_timed) |
| 6 | flat and still, 5 uploads in a row | C: B + PSRAM at 40 MHz | [run6_psram40_upload_x5](run6_psram40_upload_x5) |
| 7 | still: 1 upload and 3 restarts; then moved during a restart | D: C + 1 s calibration | [run7_calibration_1s](run7_calibration_1s) |
| 8 | 6-side accelerometer calibration (3 tries), then restarts lying still | E: D + accelerometer calibration | [run8_accel_calibration](run8_accel_calibration) |

Each folder has the unedited monitor output (colour codes removed).

Firmware versions:

- **A**: the T-010 test moved to ESP-IDF (new I2C driver, same steps as before).
- **B**: the sensor's built-in low-pass filter is switched on (register 0x1A = 0x03, gyro about 41 Hz). See
  problem 1 below.
- **C**: same IMU code as B. The board's memory chip (PSRAM) runs at 40 MHz instead of 80 MHz. See problem 2.
- **D**: C + gyro calibration over 1 s (100 samples 10 ms apart) instead of about 40 ms. See problem 4.
- **E**: D + 6-side accelerometer calibration (key C in the monitor), saved in NVS and applied to every reading;
  key R removes it. See problem 5.

## Test cases

| ID | What | How | Expected | Result | Status | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| TC-IMU-01 | Sensor found on boot | Upload, monitor | `IMU found at 0x68, WHO_AM_I=..`, then `IMU DETECTED` | `IMU found at 0x68, WHO_AM_I=0x70 MPU6500 (clone)`, `IMU DETECTED` on every boot (11 boots in runs 1-6) | Pass | run5 lines 84-87 |
| TC-IMU-02 | Gyro calibration when still | Board still at start | `IMU gyroscope bias ...` with spread <= 2.0 deg/s | Firmware D (1 s): 4 of 4 boots accepted, spread 1.9 (right after the upload), 0.8, 0.3, 0.3; bias x=-2.12..-2.18 y=2.18..2.20 z=-0.20..-0.22. Older runs 4-6 (40 ms calibration) spread 0.9-1.6 | Pass | run7 still_*_log.txt line 30 |
| TC-IMU-03 | Calibration refuses a moving board | Move the board during calibration | `calibration rejected: the sensor moved`, 5 tries, then a warning | Firmware D: 5 of 5 rejected (spread 324.9-500.3), then `calibration skipped after 5 attempts`, `IMU DETECTED`, no crash. First reading at 7398 ms against 3398 ms when still: each try takes about 1 s. Older runs 1, 2 also rejected | Pass | run7 moving_restart_log.txt lines 30-36 |
| TC-IMU-04 | One reading every 200 ms | Leave it running | `IMU #n` every 200 ms, numbering continuous | run5: 112 full readings, every gap exactly 200 ms (from `t=`), #1 to #112 with no gaps (line #113 was cut off when the recording stopped); run6: 36 readings per boot, all gaps 200 ms | Pass | run5 lines 89-200 |
| TC-IMU-05 | Still board reads 1 g, gyro near 0 | Board still after calibration | `\|a\|` 0.95-1.05 g; gx, gy, gz within +-1 deg/s | Accelerometer, firmware E with the saved calibration: `\|a\|` 0.99-1.01 g over 91 readings (before the calibration: 1.06-1.08 g flat). Gyro: within -1.3..+1.1 deg/s after a good gyro calibration (runs 4-6); in run 8 the gyro calibration was refused on a still board (spread 4.6-7.3 deg/s), so the gyro ran uncorrected | Accelerometer: Pass. Gyro: Fail (just outside), open | run8 after_usb_replug_log.txt lines 37-40; run5 |
| TC-IMU-06 | Six sides | Hold each side still | One axis about +-1 g, the others small | +X ax=+1.00..+1.04, -X ax=-1.08..-0.91 (hand-held), +Y ay=+0.94..+0.96, -Y ay=-1.01..-0.96, +Z az=+0.97..+0.99, -Z az=-0.95..-0.93 | Pass | run4 lines 361-370, 444-463, 131-352, 388-402; run5 lines 89-200; run1 lines 250-397 |
| TC-IMU-07 | Lost connection: no crash | Connection drops while running | `IMU LOST: I2C read failed ...`, no `Guru Meditation` | 2 of 2 drops in run4 (during hard movement, not a pulled wire): `IMU LOST`, no crash. No `Guru Meditation` or `abort` in any log | Pass (the drop was not planned) | run4 lines 563, 584 |
| TC-IMU-08 | Connection back: recovery | Connection comes back | `IMU BACK`, numbering carries on | 2 of 2: `IMU BACK`, #432 -> #433 and #440 -> #441 | Pass | run4 lines 562-576, 583-597 |
| TC-IMU-09 | Moving after reconnect keeps the last bias | Board still moving when it comes back | `calibration skipped ... keeping the last bias x=.. y=.. z=..` | 2 of 2: 5 rejections each, then `keeping the last bias x=-2.82 y=2.64 z=0.13`, `IMU BACK` | Pass | run4 lines 569-575, 590-596 |
| TC-IMU-10 | No sensor at boot | SDA off, reset | `IMU NOT FOUND ...` every second, no crash; `IMU DETECTED` after SDA is back | Not run: the IMU wires are soldered, so SDA cannot be taken off | Not run | — |

Notes:

- TC-IMU-05: the accelerometer part is fixed by the 6-side calibration (problem 5). The gyro part is still a few
  tenths of a deg/s outside the +-1 deg/s limit; to decide with the team (accept +-1.5 deg/s for the clone, or
  average more samples).
- TC-IMU-07 and 08 were not done on purpose: the connection dropped twice while the board was moved hard. The
  wires are soldered, so a weak joint is possible. Worth checking with a multimeter before the final build.

## Problems found and fixed

1. **Gyro calibration refused on a still board.** With firmware A the board lay still (run 3) and every
   calibration try was rejected (spread 2.7-3.6 deg/s, limit 2.0). The gyro readings were noisy because the
   sensor's low-pass filter was off. Fix (firmware B): switch on the built-in filter (register 0x1A = 0x03).
   After that the spread was 0.9-1.6 deg/s on 7 of 7 still boots (runs 4-6).
2. **Board stuck after an upload.** Twice after an upload (once in the camera runs, once before run 5) the board
   stopped while starting, just before it set up its memory chip (PSRAM), and only unplugging USB helped. Seen
   2 times in about 12 uploads. Fix (firmware C): PSRAM speed 40 MHz instead of 80 MHz
   (`CONFIG_SPIRAM_SPEED_40M=y` in `sdkconfig.defaults`). After that: 6 of 6 uploads started on their own
   (run 6, plus camera run 10), the PSRAM memory test passed each time, and the camera speed did not change. If
   it is seen again, write it in this record.
3. **Clone sensor ID.** The module reports WHO_AM_I 0x70 (MPU6500 clone) instead of 0x68. The firmware accepts
   the known clone IDs (0x68, 0x70, 0x71, 0x72, 0x73, 0x98) and prints the chip name.

4. **Gyro calibration was much shorter than 1 s.** The firmware said "keep the sensor still for 1 s", but the
   5 ms pause between the 200 samples rounded down to 0 (the FreeRTOS tick is 10 ms), so the samples were taken
   within about 40 ms. The bias was still measured, but the "still" check covered only those 40 ms. Found in a code
   review (Codex), not on the board. Fix (firmware D): 100 samples 10 ms apart, so 1 s. The calibration results in
   runs 1-6 were made with the short calibration. Run 7 repeated TC-IMU-02 and 03 with firmware D: both pass,
   and the first reading now comes 1 s later (3398 ms instead of 2458 ms), as expected.

5. **Accelerometer total not 1 g on a still board.** Flat it read 1.06-1.08 g and upside down 0.97-0.98 g. A tilt
   cannot cause this (the total is the same at any angle); the clone sensor has a small offset and scale error on
   each axis. Fix (firmware E): a 6-side calibration. Type C in the monitor, then put the board on each of its 6
   sides and let go; the firmware catches each side when it has been still for 1 s, fits an offset and a scale per
   axis to the 6 poses (they do not have to be level), saves them in NVS (namespace `icvsp_imu`) and corrects every
   reading. Result on this board: offset x=+0.013 y=+0.019 z=+0.048 g, scale x=1.001 y=1.001 z=1.015; every side
   reads 1.000 g after correction (0.966-1.062 g before). The fit is tested on the laptop with tilted sides, a
   missing side and a moving pose ([tools/test_accel_cal.cpp](../../code/imu/test_accel_cal.cpp), 8 of 8 pass).
   The first two tries timed out with 5 and 4 sides: standing on its edges the board leans up to 45 degrees, and
   the -X side was hard to find. Fixes: 5 minutes instead of 3, a new round keeps the sides already caught, and an
   `IMU CAL now: <side> up` line every 2 s shows which side faces up.
6. **Sensor gave zeros or junk after the calibration (open).** After the calibration and a restart from the laptop,
   the sensor answered its ID but read all zeros or full scale (250 deg/s) on a still board and kept dropping out
   (run 8 `restart_sensor_faulty_log.txt`). Unplugging USB and plugging it back fixed it. Not proven, but a likely cause: a chip that answers its ID
   but gives no data is often short of power; the board was turned onto its edges many times just before, and the
   drops in run 4 also came during hard movement. Check the VCC and GND joints of the IMU with a multimeter.

## Next steps

1. Decide the gyro limit of TC-IMU-05 with the team.
2. Check the IMU VCC and GND joints (problem 6).
3. TC-IMU-10 needs a way to disconnect SDA (for example a header instead of soldered wires).
