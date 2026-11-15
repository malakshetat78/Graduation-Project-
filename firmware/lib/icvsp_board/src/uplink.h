#pragma once
#include <cstddef>
#include <cstdint>

#include "report_queue.h"

namespace icvsp {

// What the HTTPS client must provide. The board version (src/board_esp32.cpp) uses WiFiClientSecure
// with the backend's CA certificate; the tests use a fake.
class ReportTransport {
public:
    virtual ~ReportTransport() = default;
    // POSTs one signed report line as the request body. Returns the HTTP status, or 0 when there was no
    // answer at all (no connection, DNS, TLS handshake or certificate failure, timeout).
    virtual int post_report(const char* line, size_t len) = 0;
};

// Sends queued reports to the backend's POST /reports one at a time, oldest first, and removes a report
// only when the backend has answered for it. Mirrors the phone's ReportUploader so both behave alike:
//   201 / 200            stored (accepted or duplicate): remove it
//   400 401 403 409 413 422   final refusal (unknown or revoked device, bad schema, stale...): remove it,
//                        count it, never retry (retrying cannot change the answer)
//   anything else, or 0  try again later: 2 s, 4 s, 8 s ... up to 5 min, +-20 % jitter
class Uplink {
public:
    struct Stats {
        uint32_t stored = 0, refused = 0, failures = 0;
        int last_status = -1;
    };

    // random_unit: returns a value in [0, 1) for the jitter (esp_random() on the board).
    Uplink(ReportQueue& queue, ReportTransport& transport, float (*random_unit)());

    // Call often. Sends at most one report per call, and nothing while offline or waiting out a retry.
    void tick(uint32_t now_ms, bool online);

    static uint32_t retry_delay_ms(uint32_t failures, float random_unit);
    static bool is_final(int status);

    const Stats& stats() const { return stats_; }
    uint32_t consecutive_failures() const { return failures_; }

private:
    ReportQueue& queue_;
    ReportTransport& transport_;
    float (*random_unit_)();
    uint32_t failures_ = 0;
    uint32_t next_try_ms_ = 0;
    bool waiting_ = false;
    Stats stats_;
};

}  // namespace icvsp
