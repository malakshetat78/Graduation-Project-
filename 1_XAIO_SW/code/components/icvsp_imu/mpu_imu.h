#pragma once
// MPU6050 (and the register-compatible clones accepted by the T-010 test) as the ECU's IMU: implements
// icvsp::ImuSource for the all-in-one firmware (main/ecu_main.cpp). The T-010 test (imu_test.cpp) is separate
// and unchanged.
//
//   - 100 Hz: sample-rate divider 9 with the low-pass filter on (1 kHz / (1 + 9)), +-2 g, filter DLPF_CFG=3
//     (about 44 Hz), the same filter as the T-010 runs.
//   - read() returns a sample only when the chip says a new one is ready (INT_STATUS data-ready), so the same
//     reading is never counted twice. It is stamped with the time it was read.
//   - The 6-side accelerometer calibration made by the T-010 firmware (NVS icvsp_imu/acc_cal_v1) is applied
//     when present.
//   - Output in the vehicle frame used by the detector: x forward, y left, z up, m/s^2, gravity included.
//   - A failed read, or no new sample for 200 ms: IMU LOST, then a reconnect attempt at most once a second
//     (IMU BACK). A reconnect blocks the caller for up to ~100 ms once a second, only while the sensor is lost.
//     discontinuities() lets the caller reset the detector after a loss or a long gap; the sample that ends a
//     long gap is dropped, so the reset comes before any sample from after the gap.
#include <cstdint>

#include "accel_cal.h"
#include "driver/i2c_master.h"
#include "imu_source.h"

class MpuImu : public icvsp::ImuSource {
public:
    struct Pins {
        i2c_port_num_t port;  // port 0 is the camera (SCCB)
        int sda_gpio;
        int scl_gpio;
        uint32_t speed_hz;
    };

    // How the board sits in the car. The detector needs z up; "forward" is the sensor axis pointing to the front.
    enum class Mount { FlatXForward, FlatYForward };

    MpuImu(const Pins& pins, Mount mount) : pins_(pins), mount_(mount) {}

    bool begin() override;                                   // false: no sensor answered (retried in read)
    bool read(uint32_t now_ms, icvsp::ImuSample& out) override;
    const char* name() const override { return name_; }

    bool connected() const { return dev_ != nullptr; }
    bool calibrated() const { return have_cal_; }
    uint32_t samples() const { return samples_; }
    uint32_t gaps() const { return gaps_; }          // more than 15 ms between two samples (one or more missed)
    uint32_t clipped() const { return clipped_; }    // an axis at the +-2 g limit
    uint32_t lost_count() const { return lost_; }
    // Lost + long gaps (over 100 ms with no sample): each one means the detector must start again.
    uint32_t discontinuities() const { return lost_ + long_gaps_; }

private:
    bool start_bus();
    bool connect(bool quiet);
    void lost(const char* why);
    bool write_reg(uint8_t reg, uint8_t value);
    bool read_regs(uint8_t reg, uint8_t* buf, size_t n);

    Pins pins_;
    Mount mount_;
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t dev_ = nullptr;
    const char* name_ = "MPU6050 (not found)";
    icvsp::AccelCal cal_;
    bool have_cal_ = false;
    bool ever_connected_ = false;
    uint32_t next_try_ms_ = 0;
    uint32_t last_sample_ms_ = 0;
    uint32_t samples_ = 0, gaps_ = 0, clipped_ = 0, lost_ = 0, long_gaps_ = 0;
    bool accel_config2_ = false;  // MPU6500 family: separate accelerometer filter register 0x1D
};
