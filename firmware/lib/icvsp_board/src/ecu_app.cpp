#include "ecu_app.h"

namespace icvsp {

const char* to_string(EcuApp::State s) {
    return s == EcuApp::State::Running ? "running" : "waiting-for-gnss-time";
}

void EcuApp::queue_sink(const char* line, void* queue) { static_cast<ReportQueue*>(queue)->push(line); }

void EcuApp::tick(uint32_t now_ms) {
    wifi_.tick(now_ms);

    const GnssFix fix = gnss_.fix(now_ms);
    if (state_ == State::WaitingForTime && fix.utc_ms > 0) {
        state_ = State::Running;
        running_since_ms_ = now_ms;
    }

    // Samples taken before GNSS time was known can only show up within kMaxSampleAgeMs of that moment;
    // after that window the age check alone is enough (and no comparison with a boot-era time is kept).
    const bool just_started = state_ == State::Running && now_ms - running_since_ms_ <= kMaxSampleAgeMs;

    ImuSample s;
    for (int i = 0; i < 16 && imu_.read(now_ms, s); ++i) {  // bounded: never starve Wi-Fi and the uplink
        const int32_t age = static_cast<int32_t>(now_ms - s.t_ms);  // wrap-safe
        if (state_ != State::Running || (just_started && static_cast<int32_t>(s.t_ms - running_since_ms_) < 0)) {
            ++imu_discarded_;  // taken before GNSS time was known
        } else if (age < 0 || static_cast<uint32_t>(age) > kMaxSampleAgeMs) {
            ++imu_stale_;
        } else {
            GnssFix at = fix;
            if (at.utc_ms > 0) at.utc_ms -= age;
            device_.on_imu(s, at);
            last_sample_utc_ms_ = at.utc_ms;
            ++imu_samples_;
        }
    }

    uplink_.tick(now_ms, wifi_.online());
}

}  // namespace icvsp
