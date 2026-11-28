#pragma once
// Accelerometer offset and scale calibration from still poses (no hardware calls, so it is tested on the laptop:
// 5_TESTING/code/imu/test_accel_cal.cpp).
//
// Each axis of the sensor reads   raw = scale * true + offset.   Gravity is 1 g in every still pose, so the
// offsets and scales are the values that make |(raw - offset) / scale| = 1 g for every pose. With one still pose
// per side (+X, -X, +Y, -Y, +Z, -Z) this has a single answer, found by Gauss-Newton. The poses do not have to be
// exactly level: a tilted pose still has a total of 1 g.
#include <cstddef>

namespace icvsp {

struct AccelCal {
    float offset[3] = {0, 0, 0};  // g
    float scale[3] = {1, 1, 1};
};

// Fits the calibration to n still poses (each the average raw reading in g). Needs n >= 6 and the poses must
// cover both directions of every axis. Returns false if the fit fails or gives implausible values (an offset over
// 0.25 g or a scale outside 0.85-1.15: then a pose was not still, or a side was missed); out is left unchanged.
// rms_g gets the root-mean-square error of |a| - 1 g over the poses after the fit.
bool fit_accel_cal(const float (*poses)[3], size_t n, AccelCal& out, float& rms_g);

// Corrected value of one reading.
inline void apply_accel_cal(const AccelCal& c, const float raw[3], float out[3])
{
    for (int k = 0; k < 3; k++) out[k] = (raw[k] - c.offset[k]) / c.scale[k];
}

}  // namespace icvsp
