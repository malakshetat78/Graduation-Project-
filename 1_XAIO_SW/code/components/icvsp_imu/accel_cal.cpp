#include "accel_cal.h"

#include <cmath>
#include <utility>

namespace icvsp {

namespace {

// Solves a 6x6 system A x = b with partial pivoting. Returns false if it is singular.
bool solve6(double a[6][6], double b[6], double x[6])
{
    for (int c = 0; c < 6; c++) {
        int p = c;
        for (int r = c + 1; r < 6; r++) {
            if (std::fabs(a[r][c]) > std::fabs(a[p][c])) p = r;
        }
        if (std::fabs(a[p][c]) < 1e-12) return false;
        if (p != c) {
            for (int k = 0; k < 6; k++) std::swap(a[p][k], a[c][k]);
            std::swap(b[p], b[c]);
        }
        for (int r = c + 1; r < 6; r++) {
            const double f = a[r][c] / a[c][c];
            for (int k = c; k < 6; k++) a[r][k] -= f * a[c][k];
            b[r] -= f * b[c];
        }
    }
    for (int r = 5; r >= 0; r--) {
        double s = b[r];
        for (int k = r + 1; k < 6; k++) s -= a[r][k] * x[k];
        x[r] = s / a[r][r];
    }
    return true;
}

}  // namespace

bool fit_accel_cal(const float (*poses)[3], size_t n, AccelCal& out, float& rms_g)
{
    if (n < 6) return false;
    // Both directions of every axis must be present, or the offset of that axis cannot be told from its scale.
    for (int k = 0; k < 3; k++) {
        bool pos = false, neg = false;
        for (size_t i = 0; i < n; i++) {
            if (poses[i][k] > 0.5f) pos = true;
            if (poses[i][k] < -0.5f) neg = true;
        }
        if (!pos || !neg) return false;
    }
    double p[6] = {0, 0, 0, 1, 1, 1};  // offsets, then scales
    for (int iter = 0; iter < 50; iter++) {
        double jtj[6][6] = {};
        double jtr[6] = {};
        for (size_t i = 0; i < n; i++) {
            double u[3];
            double norm2 = 0;
            for (int k = 0; k < 3; k++) {
                u[k] = (poses[i][k] - p[k]) / p[3 + k];
                norm2 += u[k] * u[k];
            }
            const double norm = std::sqrt(norm2);
            if (norm < 1e-6) return false;
            const double r = norm - 1.0;
            double j[6];
            for (int k = 0; k < 3; k++) {
                j[k] = -(u[k] / norm) / p[3 + k];          // d r / d offset_k
                j[3 + k] = -(u[k] / norm) * u[k] / p[3 + k];  // d r / d scale_k
            }
            for (int a = 0; a < 6; a++) {
                jtr[a] += j[a] * r;
                for (int b = 0; b < 6; b++) jtj[a][b] += j[a] * j[b];
            }
        }
        for (int a = 0; a < 6; a++) {
            jtj[a][a] += 1e-9;  // tiny damping so an exactly determined system stays solvable
            jtr[a] = -jtr[a];
        }
        double step[6];
        if (!solve6(jtj, jtr, step)) return false;
        double biggest = 0;
        for (int a = 0; a < 6; a++) {
            p[a] += step[a];
            biggest = std::fmax(biggest, std::fabs(step[a]));
        }
        if (biggest < 1e-9) break;
    }
    double sum2 = 0;
    for (size_t i = 0; i < n; i++) {
        double norm2 = 0;
        for (int k = 0; k < 3; k++) {
            const double u = (poses[i][k] - p[k]) / p[3 + k];
            norm2 += u * u;
        }
        const double r = std::sqrt(norm2) - 1.0;
        sum2 += r * r;
    }
    const double rms = std::sqrt(sum2 / static_cast<double>(n));
    for (int k = 0; k < 3; k++) {
        if (!std::isfinite(p[k]) || !std::isfinite(p[3 + k])) return false;
        if (std::fabs(p[k]) > 0.25 || p[3 + k] < 0.85 || p[3 + k] > 1.15) return false;
    }
    if (rms > 0.02) return false;
    for (int k = 0; k < 3; k++) {
        out.offset[k] = static_cast<float>(p[k]);
        out.scale[k] = static_cast<float>(p[3 + k]);
    }
    rms_g = static_cast<float>(rms);
    return true;
}

}  // namespace icvsp
