#include "uplink.h"

#include <cstdio>
#include <cstring>

namespace icvsp {

namespace {
constexpr uint32_t kFirstDelayMs = 2000;
constexpr uint32_t kMaxDelayMs = 300000;
}  // namespace

Uplink::Uplink(ReportQueue& queue, ReportTransport& transport, float (*random_unit)(), uint32_t (*clock_ms)())
    : queue_(queue), transport_(transport), random_unit_(random_unit), clock_ms_(clock_ms) {}

uint32_t Uplink::retry_delay_ms(uint32_t failures, float random_unit) {
    const uint32_t shift = failures < 8 ? failures : 8;
    uint32_t base = kFirstDelayMs << shift;
    if (base > kMaxDelayMs) base = kMaxDelayMs;
    if (random_unit < 0) random_unit = 0;
    if (random_unit > 1) random_unit = 1;
    return static_cast<uint32_t>(base * (0.8f + random_unit * 0.4f) + 0.5f);
}

bool Uplink::is_stored_ack(const char* line, const char* body) {
    if (line == nullptr || body == nullptr || std::strstr(body, "\"ack\":\"stored\"") == nullptr) return false;
    static const char kKey[] = "\"msg_id\":\"";
    const char* id = std::strstr(line, kKey);
    if (id == nullptr) return false;
    id += sizeof kKey - 1;
    const char* end = std::strchr(id, '"');
    if (end == nullptr || end == id || end - id > 100) return false;
    char want[128];
    std::snprintf(want, sizeof want, "\"event_id\":\"%.*s\"", static_cast<int>(end - id), id);
    return std::strstr(body, want) != nullptr;
}

bool Uplink::is_final(int status) {
    switch (status) {
        case 400: case 401: case 403: case 409: case 413: case 422: return true;
        default: return false;
    }
}

void Uplink::tick(uint32_t now_ms, bool online) {
    if (!online || queue_.empty()) return;
    if (waiting_ && static_cast<int32_t>(now_ms - next_try_ms_) < 0) return;  // wrap-safe
    waiting_ = false;

    const char* line = queue_.front();
    const int status = transport_.post_report(line, std::strlen(line));
    stats_.last_status = status;
    if (status == 200 || status == 201) {
        queue_.pop();
        ++stats_.stored;
        failures_ = 0;
    } else if (is_final(status)) {
        queue_.pop();
        ++stats_.refused;
        failures_ = 0;
    } else {
        ++stats_.failures;
        const uint32_t ended_ms = clock_ms_ ? clock_ms_() : now_ms;
        next_try_ms_ = ended_ms + retry_delay_ms(failures_, random_unit_ ? random_unit_() : 0.5f);
        waiting_ = true;
        if (failures_ < UINT32_MAX) ++failures_;
    }
}

}  // namespace icvsp
