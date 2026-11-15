#pragma once
#include "types.h"

namespace icvsp {

// The IMU. The parts list names an MPU6050 on I2C (to be confirmed when the hardware arrives); its driver
// implements this interface, so the app and the tests do not depend on the part. A different IMU later
// needs only another class implementing it.
//
// read() returns one sample in the vehicle frame used by the detector: x forward, y left, z up, in m/s^2,
// z including gravity (about +9.81 when level). Mounting rotation and unit conversion belong in the driver.
class ImuSource {
public:
    virtual ~ImuSource() = default;
    virtual bool begin() = 0;                          // false: part missing or not answering
    virtual bool read(uint32_t now_ms, ImuSample& out) = 0;  // false: no new sample
    virtual const char* name() const = 0;
};

// Used when no IMU driver is present: never produces samples, so the board runs (GNSS, Wi-Fi, queue, uplink)
// without inventing hazards.
class NoImu : public ImuSource {
public:
    bool begin() override { return false; }
    bool read(uint32_t, ImuSample&) override { return false; }
    const char* name() const override { return "none (no IMU driver)"; }
};

}  // namespace icvsp
