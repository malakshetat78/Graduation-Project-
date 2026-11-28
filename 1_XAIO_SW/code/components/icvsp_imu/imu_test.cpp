// T-010 MPU6050 IMU bring-up test, ESP-IDF build (CONFIG_ICVSP_APP_IMU). Same behaviour and output lines as the
// first Arduino version by Rana, on ESP-IDF's I2C master driver.
//
// Wiring (XIAO ESP32-S3): SDA -> D4 (GPIO 5), SCL -> D5 (GPIO 6), VCC -> 3V3, GND -> GND (main/board_pins.h).
// Ranges: accelerometer +-2 g (16384 LSB/g), gyroscope +-250 deg/s (131 LSB per deg/s).
// The gyroscope bias is measured at every (re)connection: keep the sensor still for about one second then.
// If it keeps moving after a reconnect, the last good bias is kept (same sensor).
// Accelerometer: offsets and scales from a 6-side calibration (type C in the monitor, then turn the board onto
// each side and hold it still), saved in NVS and applied to every reading. R removes the saved calibration.
#include "imu_test.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "accel_cal.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace {

constexpr uint16_t kAddresses[] = {0x68, 0x69};  // AD0 low / AD0 high
constexpr int kTimeoutMs = 50;                   // per I2C transfer; a missing wire fails within this time
constexpr float kStillSpreadDps = 2.0f;          // max-min per axis while calibrating; more means the sensor moved
constexpr int kCalAttempts = 5;

void log_line(const char* format, ...) __attribute__((format(printf, 1, 2)));

void log_line(const char* format, ...)
{
    char text[256];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    fputs(text, stdout);
    fflush(stdout);
}

uint32_t millis() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

ImuPins g_pins{};
i2c_master_bus_handle_t g_bus = nullptr;
i2c_master_dev_handle_t g_dev = nullptr;
uint16_t g_addr = 0;
bool g_ready = false;
bool g_ever_connected = false;
uint32_t g_reading_count = 0;
float g_bias[3] = {0, 0, 0};
bool g_have_bias = false;  // true once one calibration succeeded

// ---- Accelerometer 6-side calibration ------------------------------------------------------------------------
constexpr char kNvsNamespace[] = "icvsp_imu";
constexpr char kNvsKey[] = "acc_cal_v1";
constexpr int kStillWindow = 5;              // readings (1 s at 200 ms) that must agree for a still pose
constexpr float kStillAccelSpread = 0.03f;   // g, max-min per axis over the window
constexpr float kStillGyroDps = 5.0f;        // every gyro axis below this over the window
constexpr uint32_t kCalTimeoutMs = 300000;   // give up after 5 minutes
const char* const kSideNames[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};

icvsp::AccelCal g_acal;
bool g_have_acal = false;
bool g_nvs_ok = false;
bool g_keys_ok = false;
bool g_cal_mode = false;
uint32_t g_cal_start_ms = 0;
uint32_t g_cal_hint_ms = 0;
float g_win_a[kStillWindow][3];
float g_win_g[kStillWindow][3];
int g_win_count = 0;
float g_side_pose[6][3];
bool g_side_done[6] = {};

void start_storage_and_keys()
{
    const esp_err_t err = nvs_flash_init();
    g_nvs_ok = err == ESP_OK;
    if (!g_nvs_ok) {  // never erased here: the report firmware keeps its device secrets in the same NVS
        log_line("IMU WARN NVS not usable (%s): the accelerometer calibration cannot be saved\n", esp_err_to_name(err));
    }
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    g_keys_ok = usb_serial_jtag_driver_install(&config) == ESP_OK;
    if (g_keys_ok) usb_serial_jtag_vfs_use_driver();  // printf goes through the same driver as the key input
}

void print_acal(const char* what)
{
    log_line("IMU accel calibration %s: offset x=%+.3f y=%+.3f z=%+.3f g, scale x=%.3f y=%.3f z=%.3f\n", what,
             g_acal.offset[0], g_acal.offset[1], g_acal.offset[2], g_acal.scale[0], g_acal.scale[1], g_acal.scale[2]);
}

void load_acal()
{
    nvs_handle_t h;
    if (g_nvs_ok && nvs_open(kNvsNamespace, NVS_READONLY, &h) == ESP_OK) {
        icvsp::AccelCal c;
        size_t len = sizeof c;
        const bool ok = nvs_get_blob(h, kNvsKey, &c, &len) == ESP_OK && len == sizeof c;
        nvs_close(h);
        bool plausible = ok;
        for (int k = 0; k < 3 && ok; k++) {
            if (!(fabsf(c.offset[k]) <= 0.25f) || !(c.scale[k] >= 0.85f && c.scale[k] <= 1.15f)) plausible = false;
        }
        if (plausible) {
            g_acal = c;
            g_have_acal = true;
            print_acal("loaded");
            return;
        }
    }
    log_line("IMU accel calibration: none saved, readings are uncorrected (type C in the monitor to calibrate)\n");
}

bool save_acal(const icvsp::AccelCal& c)
{
    nvs_handle_t h;
    if (!g_nvs_ok || nvs_open(kNvsNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
    bool ok = nvs_set_blob(h, kNvsKey, &c, sizeof c) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}

void remove_acal()
{
    nvs_handle_t h;
    if (g_nvs_ok && nvs_open(kNvsNamespace, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_key(h, kNvsKey);
        nvs_commit(h);
        nvs_close(h);
    }
    g_acal = icvsp::AccelCal{};
    g_have_acal = false;
    log_line("IMU accel calibration removed: readings are uncorrected\n");
}

void print_missing_sides()
{
    char text[64] = "";
    for (int i = 0; i < 6; i++) {
        if (!g_side_done[i]) {
            strlcat(text, " ", sizeof text);
            strlcat(text, kSideNames[i], sizeof text);
        }
    }
    log_line("IMU CAL turn the board so each side faces up and hold it still 2 s; still needed:%s\n", text);
}

void start_accel_cal()
{
    g_cal_mode = true;
    g_cal_start_ms = g_cal_hint_ms = millis();
    g_win_count = 0;  // sides caught in an earlier round since this boot are kept (a timeout keeps them too)
    log_line("IMU CAL BEGIN 6-side accelerometer calibration (readings paused, 5 minutes max)\n");
    print_missing_sides();
}

void finish_accel_cal()
{
    g_cal_mode = false;
    for (bool& d : g_side_done) d = false;  // a later round starts from nothing
    icvsp::AccelCal fit;
    float rms = 0;
    if (!icvsp::fit_accel_cal(g_side_pose, 6, fit, rms)) {
        log_line("IMU CAL FAILED: the six poses do not fit (a side was not still?). Nothing saved, type C to retry\n");
        return;
    }
    g_acal = fit;
    g_have_acal = true;
    print_acal(save_acal(fit) ? "saved" : "used until reset (NOT saved)");
    for (int i = 0; i < 6; i++) {
        float v[3];
        icvsp::apply_accel_cal(g_acal, g_side_pose[i], v);
        log_line("IMU CAL check %s: |a| %.3f g before, %.3f g after\n", kSideNames[i],
                 sqrtf(g_side_pose[i][0] * g_side_pose[i][0] + g_side_pose[i][1] * g_side_pose[i][1] +
                       g_side_pose[i][2] * g_side_pose[i][2]),
                 sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
    }
    log_line("IMU CAL DONE, readings carry on (corrected)\n");
}

// One raw reading during the calibration: finds still poses and keeps one per side.
void accel_cal_step(const float a[3], const float g[3])
{
    const uint32_t now = millis();
    if (now - g_cal_start_ms > kCalTimeoutMs) {
        g_cal_mode = false;
        log_line("IMU CAL TIMED OUT after %lu s: nothing saved, type C to retry\n",
                 static_cast<unsigned long>(kCalTimeoutMs / 1000));
        return;
    }
    if (now - g_cal_hint_ms >= 10000) {
        g_cal_hint_ms = now;
        print_missing_sides();
    }
    static uint32_t last_now_line = 0;
    if (now - last_now_line >= 2000) {  // which side faces up right now, so the person can find the missing ones
        last_now_line = now;
        int ax = 0;
        for (int k = 1; k < 3; k++) {
            if (fabsf(a[k]) > fabsf(a[ax])) ax = k;
        }
        const bool turning = fabsf(g[0]) > kStillGyroDps || fabsf(g[1]) > kStillGyroDps || fabsf(g[2]) > kStillGyroDps;
        log_line("IMU CAL now: %s up (x=%+.2f y=%+.2f z=%+.2f g)%s%s\n", kSideNames[ax * 2 + (a[ax] < 0 ? 1 : 0)],
                 a[0], a[1], a[2], fabsf(a[ax]) < 0.7f ? ", between sides" : "", turning ? ", moving" : "");
    }
    const int slot = g_win_count % kStillWindow;
    for (int k = 0; k < 3; k++) {
        g_win_a[slot][k] = a[k];
        g_win_g[slot][k] = g[k];
    }
    g_win_count++;
    if (g_win_count < kStillWindow) return;
    float mean[3] = {0, 0, 0};
    for (int k = 0; k < 3; k++) {
        float lo = g_win_a[0][k], hi = g_win_a[0][k];
        for (int i = 0; i < kStillWindow; i++) {
            lo = fminf(lo, g_win_a[i][k]);
            hi = fmaxf(hi, g_win_a[i][k]);
            mean[k] += g_win_a[i][k] / kStillWindow;
            if (fabsf(g_win_g[i][k]) > kStillGyroDps) return;  // turning
        }
        if (hi - lo > kStillAccelSpread) return;  // not still yet
    }
    int axis = 0;
    for (int k = 1; k < 3; k++) {
        if (fabsf(mean[k]) > fabsf(mean[axis])) axis = k;
    }
    if (fabsf(mean[axis]) < 0.7f) return;  // between two sides
    const int side = axis * 2 + (mean[axis] < 0 ? 1 : 0);
    if (g_side_done[side]) return;
    for (int k = 0; k < 3; k++) g_side_pose[side][k] = mean[k];
    g_side_done[side] = true;
    g_win_count = 0;
    int done = 0;
    for (bool d : g_side_done) done += d ? 1 : 0;
    log_line("IMU CAL side %s captured (%d/6): x=%+.3f y=%+.3f z=%+.3f g\n", kSideNames[side], done, mean[0], mean[1],
             mean[2]);
    if (done == 6) {
        finish_accel_cal();
    } else {
        print_missing_sides();
    }
}

void check_keys()
{
    if (!g_keys_ok) return;
    char ch = 0;
    while (usb_serial_jtag_read_bytes(&ch, 1, 0) == 1) {
        if ((ch == 'C' || ch == 'c') && !g_cal_mode) {
            if (g_ready) {
                start_accel_cal();
            } else {
                log_line("IMU CAL needs the sensor connected first\n");
            }
        } else if ((ch == 'R' || ch == 'r') && !g_cal_mode) {
            remove_acal();
        }
    }
}

void remove_device()
{
    if (g_dev != nullptr) {
        i2c_master_bus_rm_device(g_dev);
        g_dev = nullptr;
    }
    g_addr = 0;
}

bool add_device(uint16_t addr)
{
    remove_device();
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = addr;
    dc.scl_speed_hz = g_pins.speed_hz;
    if (i2c_master_bus_add_device(g_bus, &dc, &g_dev) != ESP_OK) {
        g_dev = nullptr;
        return false;
    }
    g_addr = addr;
    return true;
}

// Starts the I2C bus. After a loss the bus can be left half-way through a transfer, so it is deleted and made
// again before each retry (the same idea as Wire.end()/Wire.begin() in the Arduino version).
bool start_bus()
{
    remove_device();
    if (g_bus != nullptr) {
        i2c_del_master_bus(g_bus);
        g_bus = nullptr;
    }
    i2c_master_bus_config_t bc = {};
    bc.i2c_port = g_pins.port;
    bc.sda_io_num = static_cast<gpio_num_t>(g_pins.sda_gpio);
    bc.scl_io_num = static_cast<gpio_num_t>(g_pins.scl_gpio);
    bc.clk_source = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = true;  // the breakout board has its own pull-ups too
    const esp_err_t e = i2c_new_master_bus(&bc, &g_bus);
    if (e != ESP_OK) {
        log_line("IMU I2C bus setup failed on SDA=GPIO%d SCL=GPIO%d: %s\n", g_pins.sda_gpio, g_pins.scl_gpio,
                 esp_err_to_name(e));
        g_bus = nullptr;
        return false;
    }
    return true;
}

bool write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return g_dev != nullptr && i2c_master_transmit(g_dev, data, sizeof data, kTimeoutMs) == ESP_OK;
}

bool read_regs(uint8_t reg, uint8_t* buf, size_t n)
{
    return g_dev != nullptr && i2c_master_transmit_receive(g_dev, &reg, 1, buf, n, kTimeoutMs) == ESP_OK;
}

// WHO_AM_I values of the MPU6050 and the register-compatible parts sold as "MPU6050" boards.
const char* known_imu_name(uint8_t who)
{
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
// An unknown device is printed (so it can be recorded) but not used. No empty "probe" transfer: reading
// WHO_AM_I checks the same thing (the empty probe crashed the Arduino I2C driver with SDA unplugged).
bool try_connect(bool quiet)
{
    uint8_t who = 0;
    bool any_answer = false;
    bool found = false;
    for (uint16_t a : kAddresses) {
        if (!add_device(a)) {
            continue;
        }
        uint8_t id = 0;
        if (!read_regs(0x75, &id, 1)) {
            remove_device();
            continue;
        }
        any_answer = true;
        if (known_imu_name(id) == nullptr) {
            if (!quiet) {
                log_line("IMU device at 0x%02X has unknown WHO_AM_I=0x%02X: not an MPU6050, ignored\n", a, id);
            }
            remove_device();
            continue;
        }
        who = id;
        found = true;
        break;
    }
    if (!found) {
        if (!quiet) {
            log_line(any_answer ? "IMU NOT FOUND: no MPU6050 at 0x68/0x69 (see the lines above)\n"
                                : "IMU NOT FOUND: no I2C answer at 0x68/0x69. Check SDA=D4 SCL=D5 VCC=3V3 GND\n");
        }
        return false;
    }
    log_line("IMU found at 0x%02X, WHO_AM_I=0x%02X %s%s\n", g_addr, who, known_imu_name(who),
             who == 0x68 ? "" : ": record this value");

    if (!write_reg(0x6B, 0x00)) return false;  // wake up, internal clock
    vTaskDelay(pdMS_TO_TICKS(50));
    if (!write_reg(0x1C, 0x00)) return false;  // accelerometer +-2 g
    if (!write_reg(0x1B, 0x00)) return false;  // gyroscope +-250 deg/s
    // Built-in low-pass filter (CONFIG register, DLPF_CFG=3): gyro about 41 Hz, accelerometer about 44 Hz.
    // With the filter off (the chip's default) a still board read 2.7-3.6 deg/s max-min over the 1 s calibration
    // on 2026-11-24, so a still board was refused as "moving". Road bumps are well below 20 Hz, so nothing needed is lost.
    if (!write_reg(0x1A, 0x03)) return false;
    vTaskDelay(pdMS_TO_TICKS(50));  // let the filter settle before the calibration samples
    return true;
}

enum class CalResult { ok, moved, io_error };

CalResult calibrate_once()
{
    // 100 samples 10 ms apart = 1 s. At least 1 tick per sample: with the 100 Hz FreeRTOS tick a delay under
    // 10 ms rounds down to 0, and the samples would all be taken within about 40 ms.
    constexpr int kSamples = 100;
    constexpr TickType_t kGap = pdMS_TO_TICKS(10) > 0 ? pdMS_TO_TICKS(10) : 1;
    float sum[3] = {0, 0, 0}, lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
    for (int i = 0; i < kSamples; i++) {
        uint8_t b[6];
        if (!read_regs(0x43, b, sizeof b)) return CalResult::io_error;
        for (int k = 0; k < 3; k++) {
            const float v = static_cast<int16_t>((b[2 * k] << 8) | b[2 * k + 1]) / 131.0f;
            sum[k] += v;
            if (i == 0 || v < lo[k]) lo[k] = v;
            if (i == 0 || v > hi[k]) hi[k] = v;
        }
        vTaskDelay(kGap);
    }
    const float spread = fmaxf(hi[0] - lo[0], fmaxf(hi[1] - lo[1], hi[2] - lo[2]));
    if (spread > kStillSpreadDps) {
        log_line("IMU calibration rejected: the sensor moved (spread %.1f deg/s > %.1f). Keep it still\n", spread,
                 kStillSpreadDps);
        return CalResult::moved;
    }
    for (int k = 0; k < 3; k++) {
        g_bias[k] = sum[k] / kSamples;
    }
    g_have_bias = true;
    log_line("IMU gyroscope bias x=%.2f y=%.2f z=%.2f deg/s (spread %.1f deg/s)\n", g_bias[0], g_bias[1], g_bias[2],
             spread);
    return CalResult::ok;
}

bool calibrate_gyroscope()
{
    log_line("IMU gyroscope calibration: keep the sensor still for 1 s\n");
    for (int attempt = 1; attempt <= kCalAttempts; attempt++) {
        const CalResult r = calibrate_once();
        if (r == CalResult::ok) return true;
        if (r == CalResult::io_error) return false;
    }
    // Still moving after every attempt. After a reconnect it is the same sensor, so the last good bias is still
    // right: keep it. At the first start there is none yet, so use zero rather than a wrong one, and say so.
    if (g_have_bias) {
        log_line("IMU WARN calibration skipped after %d attempts (sensor kept moving): keeping the last bias "
                 "x=%.2f y=%.2f z=%.2f deg/s\n",
                 kCalAttempts, g_bias[0], g_bias[1], g_bias[2]);
        return true;
    }
    g_bias[0] = g_bias[1] = g_bias[2] = 0;
    log_line("IMU WARN calibration skipped after %d attempts (sensor kept moving): gyro values are uncorrected\n",
             kCalAttempts);
    return true;
}

void connect_and_calibrate(bool quiet)
{
    if (!try_connect(quiet)) return;
    if (!calibrate_gyroscope()) {
        log_line("IMU GYROSCOPE CALIBRATION FAILED (sensor stopped answering)\n");
        return;
    }
    log_line("%s\n", g_ever_connected ? "IMU BACK" : "IMU DETECTED");
    g_ready = true;
    g_ever_connected = true;
}

void lost(const char* why)
{
    log_line("IMU LOST: %s. Retrying every %d ms\n", why, CONFIG_ICVSP_IMU_RETRY_MS);
    g_ready = false;
}

// One reading. Returns false (after printing IMU LOST) if the sensor did not answer properly.
bool read_once()
{
    uint8_t b[14];  // accel x,y,z, temperature, gyro x,y,z
    if (!read_regs(0x3B, b, sizeof b)) {
        lost("I2C read failed");
        return false;
    }
    const float raw[3] = {static_cast<int16_t>((b[0] << 8) | b[1]) / 16384.0f,
                          static_cast<int16_t>((b[2] << 8) | b[3]) / 16384.0f,
                          static_cast<int16_t>((b[4] << 8) | b[5]) / 16384.0f};
    const float gx = static_cast<int16_t>((b[8] << 8) | b[9]) / 131.0f - g_bias[0];
    const float gy = static_cast<int16_t>((b[10] << 8) | b[11]) / 131.0f - g_bias[1];
    const float gz = static_cast<int16_t>((b[12] << 8) | b[13]) / 131.0f - g_bias[2];
    // Gravity alone is about 1 g, so a raw total near 0 means the bytes were not real data (a loose SDA wire reads
    // as all zeros). Treat it like a failed read instead of printing it as a reading.
    if (sqrtf(raw[0] * raw[0] + raw[1] * raw[1] + raw[2] * raw[2]) < 0.2f) {
        lost("reading was all zeros (|a| < 0.2 g), wire loose?");
        return false;
    }
    if (g_cal_mode) {
        const float g[3] = {gx, gy, gz};
        accel_cal_step(raw, g);
        return true;  // readings are not printed during the calibration
    }
    float a[3] = {raw[0], raw[1], raw[2]};
    if (g_have_acal) icvsp::apply_accel_cal(g_acal, raw, a);
    const float ax = a[0], ay = a[1], az = a[2];
    const float total = sqrtf(ax * ax + ay * ay + az * az);
    g_reading_count++;
#ifdef CONFIG_ICVSP_IMU_PRINT_EVERY_READING
    log_line("IMU #%lu t=%lu ms ax=%+.2f ay=%+.2f az=%+.2f g |a|=%.2f g  gx=%+.1f gy=%+.1f gz=%+.1f deg/s\n",
             static_cast<unsigned long>(g_reading_count), static_cast<unsigned long>(millis()), ax, ay, az, total,
             gx, gy, gz);
#else
    if (g_reading_count % 25 == 1) {  // one line every 5 s
        log_line("IMU #%lu |a|=%.2f g\n", static_cast<unsigned long>(g_reading_count), total);
    }
#endif
    return true;
}

}  // namespace

void run_imu_test(const ImuPins& pins)
{
    g_pins = pins;
    // The driver prints its own error line for every failed transfer (one per retry while a wire is out).
    // This test prints IMU LOST / IMU NOT FOUND for the same events, so the driver's lines are switched off.
    esp_log_level_set("i2c.master", ESP_LOG_NONE);

    log_line("\nIMU BEGIN MPU6050 test (ESP-IDF build): SDA=GPIO%d SCL=GPIO%d I2C port %d, +-2 g, +-250 deg/s\n",
             pins.sda_gpio, pins.scl_gpio, static_cast<int>(pins.port));
    if (start_bus()) {
        vTaskDelay(pdMS_TO_TICKS(100));
        connect_and_calibrate(false);
    }
    start_storage_and_keys();
    load_acal();
    log_line("%s\n", g_ready ? "IMU reading every 200 ms" : "IMU not found, retrying every second");
    if (g_keys_ok) log_line("IMU keys: C = 6-side accelerometer calibration, R = remove the saved calibration\n");

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t last_retry = millis();
    for (;;) {
        check_keys();
        if (g_ready) {
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CONFIG_ICVSP_IMU_PERIOD_MS));
            if (!read_once()) {
                last_retry = millis();
            }
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
        if (millis() - last_retry < static_cast<uint32_t>(CONFIG_ICVSP_IMU_RETRY_MS)) {
            continue;
        }
        last_retry = millis();
        if (g_ever_connected || g_bus == nullptr) {
            start_bus();
        }
        if (g_bus != nullptr) {
            connect_and_calibrate(g_ever_connected);  // quiet retries after a loss, the LOST line already printed
        }
        last_wake = xTaskGetTickCount();
    }
}
