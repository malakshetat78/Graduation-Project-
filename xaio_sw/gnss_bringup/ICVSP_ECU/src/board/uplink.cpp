#include "uplink.h"

#include <cstdio>
#include <cstring>

#include "json_reply.h"

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
    if (line == nullptr || body == nullptr) return false;
    // The backend's limits are 128 / 64 characters (Python len), i.e. up to 4 UTF-8 bytes each.
    char id[128 * 4 + 1], sender[64 * 4 + 1], v[128 * 4 + 1];
    if (!json_top_string(line, "msg_id", id, sizeof id) || id[0] == '\0') return false;
    if (!json_top_string(line, "sender", sender, sizeof sender) || sender[0] == '\0') return false;
    return json_top_string(body, "ack", v, sizeof v) && std::strcmp(v, "stored") == 0 &&
           json_top_string(body, "event_id", v, sizeof v) && std::strcmp(v, id) == 0 &&
           json_top_string(body, "device_id", v, sizeof v) && std::strcmp(v, sender) == 0;
}

bool Uplink::is_unknown_device(const char* body) {
    char code[32];
    return body != nullptr && json_top_string(body, "code", code, sizeof code) && std::strcmp(code, "unknown_device") == 0;
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
    reply_[0] = '\0';
    const int status = transport_.post_report(line, std::strlen(line), reply_, sizeof reply_);
    reply_[sizeof reply_ - 1] = '\0';
    stats_.last_status = status;
    if ((status == 200 || status == 201) && is_stored_ack(line, reply_)) {
        queue_.pop();
        ++stats_.stored;
        failures_ = 0;
        not_provisioned_ = false;
        return;
    }
    if (status == 403 && is_unknown_device(reply_)) {
        ++stats_.not_provisioned;
        not_provisioned_ = true;
    } else if (is_final(status)) {
        queue_.pop();
        ++stats_.refused;
        failures_ = 0;
        not_provisioned_ = false;
        return;
    } else {
        ++stats_.failures;  // no answer, a server error, or a 2xx that is not this report's ACK
    }
    const uint32_t ended_ms = clock_ms_ ? clock_ms_() : now_ms;
    next_try_ms_ = ended_ms + retry_delay_ms(failures_, random_unit_ ? random_unit_() : 0.5f);
    waiting_ = true;
    if (failures_ < UINT32_MAX) ++failures_;
}

}  // namespace icvsp
