#pragma once
#include <cstddef>
#include <cstdint>

#include "report_queue.h"

namespace icvsp {

// What the HTTP(S) client must provide. The board version (ecu/main/http_transport.cpp) uses esp_http_client,
// checking https:// servers against ESP-IDF's root certificate bundle; the tests use a fake.
class ReportTransport {
public:
    virtual ~ReportTransport() = default;
    // POSTs one signed report line as the request body. Returns the HTTP status, or 0 when there was no
    // answer at all (no connection, DNS, TLS handshake or certificate failure, timeout), and copies the
    // response body into reply (NUL-terminated, cut to reply_cap - 1 bytes; empty when there is none).
    // The transport does not judge the answer: Uplink reads the body.
    virtual int post_report(const char* line, size_t len, char* reply, size_t reply_cap) = 0;
};

// Sends queued reports to the backend's POST /reports one at a time, oldest first, and removes a report
// only when the backend has answered for it. Mirrors the phone's ReportUploader so both behave alike:
//   201 / 200 with the backend's ACK for this report (is_stored_ack)   stored: remove it
//   403 with "code":"unknown_device"   the key is not registered yet (normal for a new board): keep the
//                        report, retry with back-off, report a provisioning fault (not_provisioned()).
//                        If registration never comes, the backend later answers 422 "stale" and it goes.
//   400 401 403 409 413 422   any other final refusal (revoked device, bad signature, bad schema, stale...):
//                        remove it, count it, never retry (retrying cannot change the answer)
//   anything else, 0, or a 200/201 without that ACK (captive portal, wrong URL)   try again later:
//                        2 s, 4 s, 8 s ... up to 5 min, +-20 % jitter
class Uplink {
public:
    // Room for the backend's longest ACK: event ID up to 128 and device ID up to 64 characters, each written
    // as an escaped surrogate pair in the worst case (12 bytes per character: 2304 bytes), plus the other
    // fields. A longer body is cut and is no ACK (the report is kept and retried).
    static constexpr size_t kReplyCap = 2560;

    struct Stats {
        uint32_t stored = 0, refused = 0, failures = 0, not_provisioned = 0;
        int last_status = -1;
    };

    // random_unit: returns a value in [0, 1) for the jitter (esp_random() on the board).
    // clock_ms: the device clock, read again after each attempt so a retry delay starts when a slow attempt
    // ended (the board's HTTP client can block for up to 10 s); without it the delay counts from now_ms.
    Uplink(ReportQueue& queue, ReportTransport& transport, float (*random_unit)(), uint32_t (*clock_ms)() = nullptr);

    // Call often. Sends at most one report per call, and nothing while offline or waiting out a retry.
    void tick(uint32_t now_ms, bool online);

    static uint32_t retry_delay_ms(uint32_t failures, float random_unit);
    static bool is_final(int status);
    // True when body is the backend's ACK for this report line, read as JSON: top-level "ack":"stored",
    // "event_id" equal to the line's top-level msg_id and "device_id" equal to its sender.
    static bool is_stored_ack(const char* line, const char* body);
    // True when body is the backend's 403 answer for a key it does not know: top-level "code":"unknown_device".
    static bool is_unknown_device(const char* body);

    const Stats& stats() const { return stats_; }
    uint32_t consecutive_failures() const { return failures_; }
    // The last answer was 403 unknown_device: register this device's public key with the backend.
    bool not_provisioned() const { return not_provisioned_; }
    const char* last_reply() const { return reply_; }

private:
    ReportQueue& queue_;
    ReportTransport& transport_;
    float (*random_unit_)();
    uint32_t (*clock_ms_)();
    uint32_t failures_ = 0;
    uint32_t next_try_ms_ = 0;
    bool waiting_ = false;
    bool not_provisioned_ = false;
    Stats stats_;
    char reply_[kReplyCap] = {};
};

}  // namespace icvsp
