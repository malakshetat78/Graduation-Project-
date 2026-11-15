#include "wifi_link.h"

namespace icvsp {

const char* to_string(WifiLink::State s) {
    switch (s) {
        case WifiLink::State::Idle: return "idle";
        case WifiLink::State::Connecting: return "connecting";
        case WifiLink::State::Online: return "online";
        case WifiLink::State::Backoff: return "backoff";
    }
    return "?";
}

void WifiLink::start_join(uint32_t now_ms) {
    driver_.begin();
    state_ = State::Connecting;
    since_ms_ = now_ms;
}

void WifiLink::back_off(uint32_t now_ms, uint32_t delay_ms) {
    state_ = State::Backoff;
    since_ms_ = now_ms;
    wait_ms_ = delay_ms;
}

void WifiLink::tick(uint32_t now_ms) {
    switch (state_) {
        case State::Idle:
            start_join(now_ms);
            break;
        case State::Connecting:
            if (driver_.connected()) {
                state_ = State::Online;
                ++connects_;
                next_backoff_ms_ = kFirstBackoffMs;
            } else if (now_ms - since_ms_ >= kJoinTimeoutMs) {
                driver_.disconnect();
                ++failed_;
                back_off(now_ms, next_backoff_ms_);
                next_backoff_ms_ = next_backoff_ms_ * 2 > kMaxBackoffMs ? kMaxBackoffMs : next_backoff_ms_ * 2;
            }
            break;
        case State::Online:
            if (!driver_.connected()) back_off(now_ms, kReconnectAfterLossMs);
            break;
        case State::Backoff:
            if (now_ms - since_ms_ >= wait_ms_) start_join(now_ms);
            break;
    }
}

}  // namespace icvsp
