#pragma once
// T-010 MPU6050 IMU bring-up test (original Arduino version by Rana). Prints one status line per event and one
// reading every period:
//   IMU #n t=<ms since boot> ms ax=.. ay=.. az=.. g |a|=.. g  gx=.. gy=.. gz=.. deg/s
// Status lines: IMU DETECTED, IMU NOT FOUND, IMU LOST, IMU BACK, calibration lines.
#include <cstdint>

#include "driver/i2c_master.h"

struct ImuPins {
    i2c_port_num_t port;  // I2C port; the camera uses port 0
    int sda_gpio;
    int scl_gpio;
    uint32_t speed_hz;
};

// Runs the test forever: looks for the sensor, calibrates the gyroscope, reads, and recovers after a lost wire.
[[noreturn]] void run_imu_test(const ImuPins& pins);
