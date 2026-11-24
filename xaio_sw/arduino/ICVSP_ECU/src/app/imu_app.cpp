// MPU6050 IMU test (Arduino Wire). Original by Rana, adapted for the ICVSP_ECU sketch.
//
// Wiring (XIAO ESP32-S3): SDA -> D4 (GPIO 5), SCL -> D5 (GPIO 6), VCC -> 3V3, GND -> GND. I2C port 0 (Wire);
// the camera driver uses port 1 in the Arduino core.
// Ranges: accelerometer +-2 g (16384 LSB/g), gyroscope +-250 deg/s (131 LSB per deg/s).
// Output, one line per reading every IMU_PERIOD_MS:
//   IMU ax=.. ay=.. az=.. g |a|=.. g  gx=.. gy=.. gz=.. deg/s
// Status lines always print (also with ICVSP_DEBUG 0): IMU DETECTED, IMU NOT FOUND, IMU LOST, IMU BACK.
// The gyroscope bias is measured at every (re)connection: keep the sensor still for about one second then.
// If it keeps moving after a reconnect, the last good bias is kept (same sensor).

#include "imu_app.h"

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "../../config.h"
#include "log.h"

namespace {

constexpr int IMU_SDA_PIN = 5;  // D4
constexpr int IMU_SCL_PIN = 6;  // D5
constexpr uint8_t IMU_ADDRESSES[] = {0x68, 0x69};  // AD0 low / AD0 high
constexpr uint32_t IMU_PERIOD_MS = 200;
constexpr uint32_t RETRY_MS = 1000;

uint8_t imu_addr = 0;
bool imu_ready = false;
bool imu_ever_connected = false;
uint32_t last_retry_time = 0;
uint32_t last_read_time = 0;
uint32_t reading_count = 0;
float gyroX_bias = 0, gyroY_bias = 0, gyroZ_bias = 0;
bool have_bias = false;  // true once one calibration succeeded

bool write_reg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(imu_addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool read_regs(uint8_t reg, uint8_t* buf, uint8_t n) {
  Wire.beginTransmission(imu_addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(imu_addr, n) != n) return false;
  for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

// WHO_AM_I values of the MPU6050 and the register-compatible parts sold as "MPU6050" boards.
const char* known_imu_name(uint8_t who) {
  switch (who) {
    case 0x68: return "MPU6050";
    case 0x70: return "MPU6500 (clone)";
    case 0x71: return "MPU9250 (clone)";
    case 0x72: return "MPU6050 clone (0x72)";
    case 0x73: return "MPU9255 (clone)";
    case 0x98: return "MPU6050 clone (0x98)";
    default: return nullptr;
  }
}

// Tries 0x68 then 0x69 and keeps the first address whose WHO_AM_I is a known value, then sets the ranges.
// An unknown device is printed (so it can be recorded) but not used.
bool try_connect_imu(bool quiet) {
  imu_addr = 0;
  uint8_t who = 0;
  bool any_answer = false;
  for (uint8_t a : IMU_ADDRESSES) {
    // No empty "probe" write here: on Arduino-ESP32 3.3 an empty Wire transmission while SDA is unplugged
    // crashes the I2C driver (Guru Meditation in i2c_master_probe). Reading WHO_AM_I checks the same thing.
    imu_addr = a;
    uint8_t id = 0;
    if (!read_regs(0x75, &id, 1)) {
      imu_addr = 0;
      continue;
    }
    any_answer = true;
    if (!known_imu_name(id)) {
      if (!quiet) log_line("IMU device at 0x%02X has unknown WHO_AM_I=0x%02X: not an MPU6050, ignored\n", a, id);
      imu_addr = 0;
      continue;
    }
    who = id;
    break;
  }
  if (!imu_addr) {
    if (!quiet) {
      log_line(any_answer ? "IMU NOT FOUND: no MPU6050 at 0x68/0x69 (see the lines above)\n"
                          : "IMU NOT FOUND: no I2C answer at 0x68/0x69. Check SDA=D4 SCL=D5 VCC=3V3 GND\n");
    }
    return false;
  }
  log_line("IMU found at 0x%02X, WHO_AM_I=0x%02X %s%s\n", imu_addr, who, known_imu_name(who),
           who == 0x68 ? "" : ": record this value");

  if (!write_reg(0x6B, 0x00)) return false;  // wake up, internal clock
  delay(50);
  if (!write_reg(0x1C, 0x00)) return false;  // accelerometer +-2 g
  if (!write_reg(0x1B, 0x00)) return false;  // gyroscope +-250 deg/s
  return true;
}

constexpr float STILL_SPREAD_DPS = 2.0f;  // max-min per axis while calibrating; more means the sensor moved
constexpr int CAL_ATTEMPTS = 5;

enum class CalResult { ok, moved, io_error };

CalResult calibrate_once() {
  const int samples = 200;
  float sum[3] = {0, 0, 0}, lo[3], hi[3];
  for (int i = 0; i < samples; i++) {
    uint8_t b[6];
    if (!read_regs(0x43, b, 6)) return CalResult::io_error;
    for (int k = 0; k < 3; k++) {
      const float v = (int16_t)((b[2 * k] << 8) | b[2 * k + 1]) / 131.0f;
      sum[k] += v;
      if (i == 0 || v < lo[k]) lo[k] = v;
      if (i == 0 || v > hi[k]) hi[k] = v;
    }
    delay(5);
  }
  const float spread = fmaxf(hi[0] - lo[0], fmaxf(hi[1] - lo[1], hi[2] - lo[2]));
  if (spread > STILL_SPREAD_DPS) {
    log_line("IMU calibration rejected: the sensor moved (spread %.1f deg/s > %.1f). Keep it still\n", spread,
             STILL_SPREAD_DPS);
    return CalResult::moved;
  }
  gyroX_bias = sum[0] / samples;
  gyroY_bias = sum[1] / samples;
  gyroZ_bias = sum[2] / samples;
  have_bias = true;
  log_line("IMU gyroscope bias x=%.2f y=%.2f z=%.2f deg/s (spread %.1f deg/s)\n", gyroX_bias, gyroY_bias,
           gyroZ_bias, spread);
  return CalResult::ok;
}

bool calibrate_gyroscope() {
  log_line("IMU gyroscope calibration: keep the sensor still for 1 s\n");
  for (int attempt = 1; attempt <= CAL_ATTEMPTS; attempt++) {
    const CalResult r = calibrate_once();
    if (r == CalResult::ok) return true;
    if (r == CalResult::io_error) return false;
  }
  // Still moving after every attempt. After a reconnect it is the same sensor, so the last good bias is still
  // right: keep it. At the first start there is none yet, so use zero rather than a wrong one, and say so.
  if (have_bias) {
    log_line("IMU WARN calibration skipped after %d attempts (sensor kept moving): keeping the last bias "
             "x=%.2f y=%.2f z=%.2f deg/s\n",
             CAL_ATTEMPTS, gyroX_bias, gyroY_bias, gyroZ_bias);
    return true;
  }
  gyroX_bias = gyroY_bias = gyroZ_bias = 0;
  log_line("IMU WARN calibration skipped after %d attempts (sensor kept moving): gyro values are uncorrected\n",
           CAL_ATTEMPTS);
  return true;
}

void connect_and_calibrate(bool quiet) {
  if (!try_connect_imu(quiet)) return;
  if (!calibrate_gyroscope()) {
    log_line("IMU GYROSCOPE CALIBRATION FAILED (sensor stopped answering)\n");
    return;
  }
  log_line("%s\n", imu_ever_connected ? "IMU BACK" : "IMU DETECTED");
  imu_ready = true;
  imu_ever_connected = true;
}

// After a loss the bus can be left half-way through a transfer: start the I2C driver again before each retry.
void restart_bus() {
  Wire.end();
  Wire.begin(IMU_SDA_PIN, IMU_SCL_PIN, 400000);
  Wire.setTimeOut(50);
}

void lost(const char* why) {
  log_line("IMU LOST: %s. Retrying every %lu ms\n", why, (unsigned long)RETRY_MS);
  imu_ready = false;
  last_retry_time = millis();
}

}  // namespace

void start_imu_app() {
  log_line("\nIMU BEGIN MPU6050 test: SDA=D4 SCL=D5, +-2 g, +-250 deg/s\n");
  step(1, 2, "IMU: look for the MPU6050 and calibrate the gyroscope");
  Wire.begin(IMU_SDA_PIN, IMU_SCL_PIN, 400000);
  Wire.setTimeOut(50);
  delay(100);
  connect_and_calibrate(false);
  step(2, 2, imu_ready ? "IMU: reading every 200 ms" : "IMU: not found, retrying every second");
}

void imu_app_loop() {
  const uint32_t now = millis();
  if (!imu_ready) {
    if (now - last_retry_time >= RETRY_MS) {
      last_retry_time = now;
      if (imu_ever_connected) restart_bus();
      connect_and_calibrate(imu_ever_connected);  // quiet retries after a loss, the LOST line already printed
    }
    return;
  }
  if (now - last_read_time < IMU_PERIOD_MS) return;
  last_read_time = now;

  uint8_t b[14];  // accel x,y,z, temperature, gyro x,y,z
  if (!read_regs(0x3B, b, 14)) {
    lost("I2C read failed");
    return;
  }
  const float ax = (int16_t)((b[0] << 8) | b[1]) / 16384.0f;
  const float ay = (int16_t)((b[2] << 8) | b[3]) / 16384.0f;
  const float az = (int16_t)((b[4] << 8) | b[5]) / 16384.0f;
  const float gx = (int16_t)((b[8] << 8) | b[9]) / 131.0f - gyroX_bias;
  const float gy = (int16_t)((b[10] << 8) | b[11]) / 131.0f - gyroY_bias;
  const float gz = (int16_t)((b[12] << 8) | b[13]) / 131.0f - gyroZ_bias;
  const float total = sqrtf(ax * ax + ay * ay + az * az);
  // Gravity alone is about 1 g, so a total near 0 means the bytes were not real data (a loose SDA wire reads
  // as all zeros). Treat it like a failed read instead of printing it as a reading.
  if (total < 0.2f) {
    lost("reading was all zeros (|a| < 0.2 g), wire loose?");
    return;
  }
  reading_count++;
#if ICVSP_DEBUG
  log_line("IMU #%lu ax=%+.2f ay=%+.2f az=%+.2f g |a|=%.2f g  gx=%+.1f gy=%+.1f gz=%+.1f deg/s\n",
           (unsigned long)reading_count, ax, ay, az, total, gx, gy, gz);
#else
  if (reading_count % 25 == 1) {  // debug off: one line every 5 s
    log_line("IMU #%lu |a|=%.2f g\n", (unsigned long)reading_count, total);
  }
#endif
}
