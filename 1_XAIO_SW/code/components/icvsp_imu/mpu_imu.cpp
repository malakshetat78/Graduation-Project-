#include "mpu_imu.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

namespace {

constexpr uint16_t kAddresses[] = {0x68, 0x69};  // AD0 low / AD0 high
constexpr int kTimeoutMs = 20;                   // per I2C transfer (one 7-byte read takes well under 1 ms)
constexpr uint32_t kRetryMs = 1000;
constexpr uint32_t kGapMs = 15;                  // 100 Hz: 10 ms apart; more than 15 ms means a sample was missed
constexpr uint32_t kLongGapMs = 100;             // the detector starts again after a gap this long
constexpr uint32_t kNoDataMs = 200;              // answering but no new sample this long: treated as lost
constexpr float kG = 9.80665f;
constexpr int16_t kClip = 32700;                 // close to the +-2 g limit (32767)

// WHO_AM_I values accepted by the T-010 test (imu_test.cpp), same list.
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

uint32_t ms_now() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }  // same clock as the caller

}  // namespace

bool MpuImu::start_bus()
{
    if (dev_ != nullptr) {
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
    }
    if (bus_ != nullptr) {  // after a loss the bus can be stuck half-way through a transfer: make it again
        i2c_del_master_bus(bus_);
        bus_ = nullptr;
    }
    i2c_master_bus_config_t bc = {};
    bc.i2c_port = pins_.port;
    bc.sda_io_num = static_cast<gpio_num_t>(pins_.sda_gpio);
    bc.scl_io_num = static_cast<gpio_num_t>(pins_.scl_gpio);
    bc.clk_source = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = true;
    if (i2c_new_master_bus(&bc, &bus_) != ESP_OK) {
        bus_ = nullptr;
        return false;
    }
    return true;
}

bool MpuImu::write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return dev_ != nullptr && i2c_master_transmit(dev_, data, sizeof data, kTimeoutMs) == ESP_OK;
}

bool MpuImu::read_regs(uint8_t reg, uint8_t* buf, size_t n)
{
    return dev_ != nullptr && i2c_master_transmit_receive(dev_, &reg, 1, buf, n, kTimeoutMs) == ESP_OK;
}

bool MpuImu::connect(bool quiet)
{
    if (!start_bus()) {
        if (!quiet) std::printf("IMU I2C bus setup failed on SDA=GPIO%d SCL=GPIO%d\n", pins_.sda_gpio, pins_.scl_gpio);
        return false;
    }
    uint8_t who = 0;
    uint16_t addr = 0;
    for (uint16_t a : kAddresses) {
        i2c_device_config_t dc = {};
        dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dc.device_address = a;
        dc.scl_speed_hz = pins_.speed_hz;
        if (i2c_master_bus_add_device(bus_, &dc, &dev_) != ESP_OK) {
            dev_ = nullptr;
            continue;
        }
        uint8_t id = 0;
        if (read_regs(0x75, &id, 1) && known_imu_name(id) != nullptr) {
            who = id;
            addr = a;
            break;
        }
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
    }
    if (dev_ == nullptr) {
        if (!quiet) std::printf("IMU NOT FOUND at 0x68/0x69 (SDA=D4 SCL=D5 VCC=3V3 GND); retrying every 1 s\n");
        return false;
    }
    // Same setup as the T-010 test, plus 100 Hz and the data-ready flag.
    bool ok = write_reg(0x6B, 0x00);  // wake up, internal clock
    vTaskDelay(pdMS_TO_TICKS(20));
    ok = ok && write_reg(0x1A, 0x03);  // low-pass filter DLPF_CFG=3 (accel ~44 Hz), internal rate 1 kHz
    ok = ok && write_reg(0x19, 9);     // sample rate = 1 kHz / (1 + 9) = 100 Hz
    ok = ok && write_reg(0x1C, 0x00);  // accelerometer +-2 g
    ok = ok && write_reg(0x1B, 0x00);  // gyroscope +-250 deg/s (not used by the detector)
    // MPU6500 family (WHO_AM_I 0x70/0x71/0x73): 0x1A filters only the gyroscope; the accelerometer has its own
    // filter in ACCEL_CONFIG2 (0x1D): A_DLPF_CFG=3, about 41 Hz. Not written on the MPU6050 (0x68) or on the
    // unknown clones (0x72, 0x98), where 0x1D is not documented.
    accel_config2_ = who == 0x70 || who == 0x71 || who == 0x73;
    if (accel_config2_) ok = ok && write_reg(0x1D, 0x03);
    ok = ok && write_reg(0x38, 0x01);  // DATA_RDY_EN: sets the data-ready flag in INT_STATUS (INT pin not wired)
    if (!ok) {
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
        if (!quiet) std::printf("IMU found but setup failed; retrying every 1 s\n");
        return false;
    }
    name_ = known_imu_name(who);
    std::printf("IMU %s: %s at 0x%02X, WHO_AM_I=0x%02X, 100 Hz, +-2 g, accel filter %s, calibration %s\n",
                ever_connected_ ? "BACK" : "DETECTED", name_, addr, who, accel_config2_ ? "0x1D=3 (41 Hz)" : "0x1A=3 (44 Hz)",
                have_cal_ ? "applied" : "none (uncorrected)");
    ever_connected_ = true;
    last_sample_ms_ = 0;
    return true;
}

bool MpuImu::begin()
{
    // The calibration from the T-010 firmware (type C there). Plausibility limits as in imu_test.cpp.
    nvs_handle_t h;
    if (nvs_open("icvsp_imu", NVS_READONLY, &h) == ESP_OK) {
        icvsp::AccelCal c;
        size_t len = sizeof c;
        const bool ok = nvs_get_blob(h, "acc_cal_v1", &c, &len) == ESP_OK && len == sizeof c;
        nvs_close(h);
        bool plausible = ok;
        for (int k = 0; k < 3 && ok; k++)
            if (!(std::fabs(c.offset[k]) <= 0.25f) || !(c.scale[k] >= 0.85f && c.scale[k] <= 1.15f)) plausible = false;
        if (plausible) {
            cal_ = c;
            have_cal_ = true;
        }
    }
    next_try_ms_ = ms_now() + kRetryMs;
    const bool ok = connect(false);
    last_sample_ms_ = ms_now();  // starts the no-data timer even before the first sample
    return ok;
}

void MpuImu::lost(const char* why)
{
    ++lost_;
    std::printf("IMU LOST (%s); retrying every 1 s\n", why);
    if (dev_ != nullptr) {
        i2c_master_bus_rm_device(dev_);
        dev_ = nullptr;
    }
    next_try_ms_ = ms_now() + kRetryMs;
}

bool MpuImu::read(uint32_t now_ms, icvsp::ImuSample& out)
{
    if (dev_ == nullptr) {
        if (static_cast<int32_t>(now_ms - next_try_ms_) < 0) return false;
        next_try_ms_ = now_ms + kRetryMs;
        connect(true);
        last_sample_ms_ = now_ms;  // the no-data timer starts at the reconnect
        return false;  // the caller's time is from before the reconnect: the next sample comes on the next call
    }
    uint8_t b[7];  // INT_STATUS (0x3A) then ACCEL_XOUT_H..ACCEL_ZOUT_L (0x3B-0x40): one transfer
    if (!read_regs(0x3A, b, sizeof b)) {
        lost("no I2C answer");
        return false;
    }
    if ((b[0] & 0x01) == 0) {  // no new sample since the last read (reading 0x3A clears the flag)
        if (last_sample_ms_ != 0 && now_ms - last_sample_ms_ > kNoDataMs) lost("answering but no new samples");
        return false;
    }
    const int16_t r[3] = {static_cast<int16_t>((b[1] << 8) | b[2]), static_cast<int16_t>((b[3] << 8) | b[4]),
                          static_cast<int16_t>((b[5] << 8) | b[6])};
    if (r[0] == 0 && r[1] == 0 && r[2] == 0) {  // all zero: the sensor is not measuring (seen when power drops)
        lost("all-zero reading");
        return false;
    }
    for (int16_t v : r)
        if (v >= kClip || v <= -kClip) {
            ++clipped_;
            break;
        }
    const float raw[3] = {r[0] / 16384.0f, r[1] / 16384.0f, r[2] / 16384.0f};
    float g[3];
    if (have_cal_) {
        icvsp::apply_accel_cal(cal_, raw, g);
    } else {
        for (int k = 0; k < 3; k++) g[k] = raw[k];
    }
    out.t_ms = now_ms;
    if (mount_ == Mount::FlatXForward) {
        out.ax = g[0] * kG;
        out.ay = g[1] * kG;
    } else {  // sensor Y forward: vehicle x = sensor y, vehicle y (left) = -sensor x
        out.ax = g[1] * kG;
        out.ay = -g[0] * kG;
    }
    out.az = g[2] * kG;
    if (last_sample_ms_ != 0 && now_ms - last_sample_ms_ > kGapMs) ++gaps_;
    if (last_sample_ms_ != 0 && now_ms - last_sample_ms_ > kLongGapMs) {
        // Long gap: this sample is dropped, so the caller sees discontinuities() change and resets the detector
        // before the next sample reaches it (an event cut by the gap is never finished with new data).
        ++long_gaps_;
        last_sample_ms_ = now_ms;
        return false;
    }
    last_sample_ms_ = now_ms;
    ++samples_;
    return true;
}
