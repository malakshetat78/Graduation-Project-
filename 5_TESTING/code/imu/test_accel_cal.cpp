// Host test for 1_XAIO_SW/code/components/icvsp_imu/accel_cal.cpp (the 6-side accelerometer calibration).
// Runs on a computer, not on the XIAO:
//   c++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined -I 1_XAIO_SW/code/components/icvsp_imu \
//       5_TESTING/code/imu/test_accel_cal.cpp 1_XAIO_SW/code/components/icvsp_imu/accel_cal.cpp -o /tmp/t && /tmp/t
#include <cmath>
#include <cstdio>

#include "accel_cal.h"

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// Raw reading of a sensor with the given offsets and scales for a true direction (unit vector), in g.
void sensor(const icvsp::AccelCal& s, double x, double y, double z, float out[3])
{
    const double n = std::sqrt(x * x + y * y + z * z);
    const double t[3] = {x / n, y / n, z / n};
    for (int k = 0; k < 3; k++) out[k] = static_cast<float>(s.scale[k] * t[k] + s.offset[k]);
}

double total_after(const icvsp::AccelCal& c, const float raw[3])
{
    float v[3];
    icvsp::apply_accel_cal(c, raw, v);
    return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

}  // namespace

int main()
{
    // A sensor with errors like the board's clone (offsets of a few 0.01 g, scales a few % off).
    icvsp::AccelCal truth;
    truth.offset[0] = 0.03f; truth.offset[1] = -0.02f; truth.offset[2] = 0.05f;
    truth.scale[0] = 1.02f;  truth.scale[1] = 0.98f;   truth.scale[2] = 1.04f;

    // Six sides, each tilted by up to about 15 degrees (a table that is not level, a board held in the hand).
    float poses[6][3];
    sensor(truth, 1, 0.2, 0.1, poses[0]);
    sensor(truth, -1, -0.15, 0.2, poses[1]);
    sensor(truth, 0.1, 1, -0.25, poses[2]);
    sensor(truth, -0.2, -1, 0.1, poses[3]);
    sensor(truth, 0.2, 0.37, 1, poses[4]);
    sensor(truth, 0.12, 0.23, -1, poses[5]);

    icvsp::AccelCal fit;
    float rms = -1;
    check(icvsp::fit_accel_cal(poses, 6, fit, rms), "six tilted sides give a calibration");
    bool close = true;
    for (int k = 0; k < 3; k++) {
        if (std::fabs(fit.offset[k] - truth.offset[k]) > 1e-3 || std::fabs(fit.scale[k] - truth.scale[k]) > 1e-3)
            close = false;
    }
    check(close, "offsets and scales found within 0.001");
    // A pose not used in the fit reads 1 g after correction, but not before.
    float other[3];
    sensor(truth, 0.6, -0.5, 0.62, other);
    check(std::fabs(total_after(fit, other) - 1.0) < 0.002, "a new pose reads 1.000 g after correction");
    check(std::fabs(std::sqrt(other[0] * other[0] + other[1] * other[1] + other[2] * other[2]) - 1.0) > 0.01,
          "the same pose was more than 0.01 g off before correction");

    // A missing side (no -Z pose) must be refused: offset and scale of Z cannot be separated.
    float five[6][3];
    for (int i = 0; i < 5; i++) for (int k = 0; k < 3; k++) five[i][k] = poses[i][k];
    sensor(truth, 0.1, 1, 0.3, five[5]);  // +Y again instead of -Z
    icvsp::AccelCal untouched;
    check(!icvsp::fit_accel_cal(five, 6, untouched, rms) && untouched.scale[2] == 1.0f,
          "a missing side is refused, calibration unchanged");
    check(!icvsp::fit_accel_cal(poses, 5, untouched, rms), "fewer than 6 poses is refused");

    // A pose taken while the board moved (1.4 g) among 7 poses: the fit cannot match it, so it is refused.
    float seven[7][3];
    for (int i = 0; i < 6; i++) for (int k = 0; k < 3; k++) seven[i][k] = poses[i][k];
    sensor(truth, 0, 0, 1, seven[6]);
    for (int k = 0; k < 3; k++) seven[6][k] *= 1.4f;
    check(!icvsp::fit_accel_cal(seven, 7, untouched, rms), "a moving pose among 7 is refused");

    // A perfect sensor: offsets 0, scales 1.
    icvsp::AccelCal ideal;
    float perfect[6][3];
    const double dirs[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int i = 0; i < 6; i++) sensor(ideal, dirs[i][0], dirs[i][1], dirs[i][2], perfect[i]);
    check(icvsp::fit_accel_cal(perfect, 6, fit, rms) && std::fabs(fit.offset[2]) < 1e-4 &&
              std::fabs(fit.scale[0] - 1) < 1e-4,
          "a perfect sensor gives offset 0 and scale 1");

    std::printf("%s\n", g_failures == 0 ? "ALL PASS" : "SOME FAILED");
    return g_failures == 0 ? 0 : 1;
}
