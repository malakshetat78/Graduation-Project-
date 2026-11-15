// Offline queue + delivery to POST /reports with retry (board layer), against a fake HTTPS client.
#include <unity.h>

#include <cstdio>
#include <string>
#include <vector>

#include "report_queue.h"
#include "json_reply.h"
#include "uplink.h"

using namespace icvsp;

namespace {

// A report line as Device builds it, reduced to the fields the uplink reads.
std::string L(const char* id) { return std::string("{\"event\":{},\"sender\":\"car\",\"msg_id\":\"") + id + "\",\"utc_ms\":1}"; }

// The backend's stored ACK for a line (accepted or duplicate).
std::string ack_for(const std::string& line, const char* status = "accepted") {
    char id[64] = {};
    json_top_string(line.c_str(), "msg_id", id, sizeof id);
    return std::string("{\"status\":\"") + status + "\",\"ack\":\"stored\",\"event_id\":\"" + id +
           "\",\"device_id\":\"car\",\"stored_at\":1.5,\"evaluation\":\"pending\"}";
}

struct FakeTransport : ReportTransport {
    std::vector<int> answers;           // consumed front to back; 201 once empty
    std::vector<std::string> replies;   // reply bodies, front to back; empty -> the backend's normal reply
    std::vector<std::string> bodies;
    int post_report(const char* line, size_t len, char* reply, size_t cap) override {
        bodies.emplace_back(line, len);
        int a = 201;
        if (!answers.empty()) {
            a = answers.front();
            answers.erase(answers.begin());
        }
        std::string r;
        if (!replies.empty()) {
            r = replies.front();
            replies.erase(replies.begin());
        } else if (a == 200 || a == 201) {
            r = ack_for(std::string(line, len), a == 200 ? "duplicate" : "accepted");
        } else {
            r = "{\"detail\":\"refused\",\"code\":\"schema\",\"retryable\":false}";
        }
        std::snprintf(reply, cap, "%s", r.c_str());
        return a;
    }
};

float half() { return 0.5f; }

uint32_t g_clock = 0;  // device clock for the slow-transport test
uint32_t clock_ms() { return g_clock; }

struct SlowTransport : ReportTransport {  // every POST takes 10 s and gets no answer (e.g. a TCP timeout)
    int posts = 0;
    int post_report(const char*, size_t, char* reply, size_t cap) override {
        if (cap) reply[0] = '\0';
        ++posts;
        g_clock += 10000;
        return 0;
    }
};

ReportQueue* q;  // ~35 KB: kept off the test's stack

}  // namespace

void setUp() { q = new ReportQueue(); }
void tearDown() { delete q; }

void test_queue_keeps_order_and_drops_the_oldest_when_full() {
    char line[16];
    for (size_t i = 0; i < ReportQueue::kCapacity + 3; ++i) {
        std::snprintf(line, sizeof line, "r%zu", i);
        TEST_ASSERT_TRUE(q->push(line));
    }
    TEST_ASSERT_EQUAL_size_t(ReportQueue::kCapacity, q->size());
    TEST_ASSERT_EQUAL_UINT32(3, q->dropped());
    TEST_ASSERT_EQUAL_STRING("r3", q->front());
    q->pop();
    TEST_ASSERT_EQUAL_STRING("r4", q->front());
}

void test_queue_refuses_empty_and_oversized_lines() {
    TEST_ASSERT_FALSE(q->push(""));
    TEST_ASSERT_FALSE(q->push(nullptr));
    std::string big(ReportQueue::kMaxLine, 'x');
    TEST_ASSERT_FALSE(q->push(big.c_str()));
    big.pop_back();
    TEST_ASSERT_TRUE(q->push(big.c_str()));  // 1099 characters: the longest line Device builds
    TEST_ASSERT_EQUAL_size_t(1, q->size());
}

// The board's HTTP client blocks for up to 10 s. The retry delay must start when the attempt ended, not when
// it began, or a slow failure would use up the whole back-off and the next try would follow at once.
void test_retry_delay_counts_from_the_end_of_a_slow_attempt() {
    SlowTransport t;
    Uplink u(*q, t, half, clock_ms);
    q->push(L("a").c_str());
    g_clock = 0;
    u.tick(g_clock, true);  // fails at 10 000; first delay 2 000 ms (jitter 0.5 -> exactly 2 000)
    TEST_ASSERT_EQUAL_INT(1, t.posts);
    u.tick(10050, true);
    TEST_ASSERT_EQUAL_INT(1, t.posts);  // still waiting
    u.tick(11999, true);
    TEST_ASSERT_EQUAL_INT(1, t.posts);
    g_clock = 12000;
    u.tick(12000, true);
    TEST_ASSERT_EQUAL_INT(2, t.posts);
}

// A 200/201 only counts when the body is the backend's ACK for this very report, read as JSON: top-level
// "ack":"stored", "event_id" = the line's msg_id and "device_id" = its sender. Anything else (a captive portal,
// a wrong URL, another report's ACK, an error object that happens to contain those words) is no answer.
void test_only_a_stored_ack_for_this_report_counts() {
    const char* line = "{\"event\":{\"sender\":\"x\"},\"sender\":\"ecu-1\",\"msg_id\":\"ecu-1-7-1\",\"utc_ms\":1}";
    const char* good = "{\"status\":\"accepted\",\"ack\":\"stored\",\"event_id\":\"ecu-1-7-1\",\"device_id\":\"ecu-1\"}";
    TEST_ASSERT_TRUE(Uplink::is_stored_ack(line, good));
    TEST_ASSERT_TRUE(Uplink::is_stored_ack(line,  // normal JSON spacing and another key order
        "{ \"device_id\" : \"ecu-1\",\n  \"event_id\": \"ecu-1-7-1\", \"ack\": \"stored\", \"stored_at\": 1.5e9, \"x\": [1, {\"a\": null}] }"));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(line, ""));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(line, "<html>Welcome to the hotel Wi-Fi</html>"));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(line,  // the words are there, but inside a nested error object
        "{\"error\":{\"ack\":\"stored\",\"event_id\":\"ecu-1-7-1\",\"device_id\":\"ecu-1\"}}"));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(line,  // ... or inside a string
        "{\"detail\":\"\\\"ack\\\":\\\"stored\\\",\\\"event_id\\\":\\\"ecu-1-7-1\\\"\",\"device_id\":\"ecu-1\"}"));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(line,  // cut off (truncated reply)
        "{\"status\":\"accepted\",\"ack\":\"stored\",\"event_id\":\"ecu-1-7-1\",\"device_id\":\"ecu-1\""));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(line,  // trailing garbage after the object
        "{\"ack\":\"stored\",\"event_id\":\"ecu-1-7-1\",\"device_id\":\"ecu-1\"} extra"));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(line,  // the same key twice: ambiguous, refused
        "{\"ack\":\"refused\",\"ack\":\"stored\",\"event_id\":\"ecu-1-7-1\",\"device_id\":\"ecu-1\"}"));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(line, "{\"ack\":\"stored\",\"event_id\":\"ecu-1-7-10\",\"device_id\":\"ecu-1\"}"));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(line, "{\"ack\":\"stored\",\"event_id\":\"ecu-1-7-1\",\"device_id\":\"ecu-2\"}"));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(line, "{\"ack\":\"stored\",\"event_id\":\"ecu-1-7-1\"}"));  // no device_id
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(line, "{\"ack\":\"queued\",\"event_id\":\"ecu-1-7-1\",\"device_id\":\"ecu-1\"}"));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(line, "{\"ack\":1,\"event_id\":\"ecu-1-7-1\",\"device_id\":\"ecu-1\"}"));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(line, "[\"ack\",\"stored\"]"));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack("{\"no_msg_id\":1}", "{\"ack\":\"stored\",\"event_id\":\"\"}"));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(nullptr, good));
    TEST_ASSERT_FALSE(Uplink::is_stored_ack(line, nullptr));
}

void test_json_reader_handles_escapes_and_rejects_bad_json() {
    char v[32];
    TEST_ASSERT_TRUE(json_top_string("{\"k\":\"a\\\"b\\\\c\\u0041\"}", "k", v, sizeof v));
    TEST_ASSERT_EQUAL_STRING("a\"b\\cA", v);
    TEST_ASSERT_FALSE(json_top_string("{\"k\":\"abc\"", "k", v, sizeof v));            // unterminated object
    TEST_ASSERT_FALSE(json_top_string("{\"k\":\"abc}", "k", v, sizeof v));              // unterminated string
    TEST_ASSERT_FALSE(json_top_string("{\"k\":01}", "k", v, sizeof v));                  // bad number
    TEST_ASSERT_FALSE(json_top_string("{\"k\":\"0123456789012345678901234567890123\"}", "k", v, sizeof v));  // too long
    TEST_ASSERT_FALSE(json_top_string("{\"k\":tru}", "k", v, sizeof v));
    TEST_ASSERT_FALSE(json_top_string("{\"a\":1,}", "a", v, sizeof v));                  // trailing comma
    std::string deep(40, '[');
    deep = "{\"a\":" + deep + std::string(40, ']') + ",\"k\":\"v\"}";
    TEST_ASSERT_FALSE(json_top_string(deep.c_str(), "k", v, sizeof v));                   // nested too deep
}

// The backend answers 403 "unknown_device" when the device's key is not registered yet. Registering it later is
// normal (first boot of a new board), so the report is kept, retried with back-off and the uplink reports a
// provisioning fault; any other 403 (e.g. "device_revoked") is final as before.
void test_unknown_device_keeps_the_report_until_registered() {
    FakeTransport t;
    Uplink u(*q, t, half);
    q->push(L("a").c_str());
    t.answers = {403, 403, 201};
    t.replies = {"{\"detail\":\"unknown device\",\"code\":\"unknown_device\",\"retryable\":false}",
                 "{\"detail\": \"unknown device\", \"code\": \"unknown_device\"}"};
    u.tick(0, true);
    TEST_ASSERT_EQUAL_size_t(1, q->size());
    TEST_ASSERT_TRUE(u.not_provisioned());
    TEST_ASSERT_EQUAL_UINT32(0, u.stats().refused);
    TEST_ASSERT_EQUAL_UINT32(1, u.stats().not_provisioned);
    u.tick(1999, true);
    TEST_ASSERT_EQUAL_size_t(1, t.bodies.size());  // waits out the back-off like any retry
    u.tick(2000, true);
    TEST_ASSERT_EQUAL_UINT32(2, u.stats().not_provisioned);
    u.tick(2000 + 4000, true);  // registered meanwhile: stored
    TEST_ASSERT_TRUE(q->empty());
    TEST_ASSERT_FALSE(u.not_provisioned());
    TEST_ASSERT_EQUAL_UINT32(1, u.stats().stored);
}

void test_other_403_and_signature_refusals_stay_final() {
    const char* replies[] = {"{\"detail\":\"device revoked\",\"code\":\"device_revoked\"}",
                             "{\"error\":{\"code\":\"unknown_device\"}}",  // nested: not the backend's answer
                             "<html>Forbidden</html>"};
    for (const char* r : replies) {
        FakeTransport t;
        Uplink u(*q, t, half);
        q->push(L("a").c_str());
        t.answers = {403};
        t.replies = {r};
        u.tick(0, true);
        TEST_ASSERT_TRUE(q->empty());
        TEST_ASSERT_EQUAL_UINT32(1, u.stats().refused);
        TEST_ASSERT_FALSE(u.not_provisioned());
    }
}

void test_success_status_without_a_valid_ack_is_retried() {
    FakeTransport t;
    Uplink u(*q, t, half);
    q->push(L("a").c_str());
    t.answers = {200, 201};
    t.replies = {"<html>Hotel Wi-Fi: please log in</html>"};
    u.tick(0, true);
    TEST_ASSERT_EQUAL_size_t(1, q->size());
    TEST_ASSERT_EQUAL_UINT32(1, u.stats().failures);
    u.tick(2000, true);
    TEST_ASSERT_TRUE(q->empty());
    TEST_ASSERT_EQUAL_UINT32(1, u.stats().stored);
}

void test_nothing_is_sent_while_offline() {
    FakeTransport t;
    Uplink u(*q, t, half);
    q->push(L("a").c_str());
    u.tick(0, false);
    TEST_ASSERT_EQUAL_size_t(0, t.bodies.size());
    TEST_ASSERT_EQUAL_size_t(1, q->size());
}

void test_stored_reports_leave_the_queue_in_order_body_unchanged() {
    FakeTransport t;
    Uplink u(*q, t, half);
    const std::string first = L("first"), second = L("second");
    const char* line = first.c_str();
    q->push(line);
    q->push(second.c_str());
    t.answers = {201, 200};  // accepted, then duplicate (already stored)
    u.tick(0, true);
    u.tick(1, true);
    TEST_ASSERT_EQUAL_size_t(2, t.bodies.size());
    TEST_ASSERT_EQUAL_STRING(line, t.bodies[0].c_str());  // the signed bytes go out exactly as built
    TEST_ASSERT_EQUAL_STRING(second.c_str(), t.bodies[1].c_str());
    TEST_ASSERT_TRUE(q->empty());
    TEST_ASSERT_EQUAL_UINT32(2, u.stats().stored);
}

void test_final_refusals_are_dropped_not_retried() {
    const int finals[] = {400, 401, 403, 409, 413, 422};  // 403 here is not "unknown_device" (see below)
    for (int status : finals) {
        FakeTransport t;
        Uplink u(*q, t, half);
        q->push(L("x").c_str());
        t.answers = {status};
        u.tick(0, true);
        TEST_ASSERT_TRUE(q->empty());
        TEST_ASSERT_EQUAL_UINT32(1, u.stats().refused);
        TEST_ASSERT_EQUAL_UINT32(0, u.consecutive_failures());
    }
}

void test_no_answer_and_server_errors_keep_the_report_and_back_off() {
    FakeTransport t;
    Uplink u(*q, t, half);
    q->push(L("x").c_str());
    t.answers = {0, 503, 429};
    u.tick(0, true);  // no connection / TLS failure
    TEST_ASSERT_EQUAL_size_t(1, q->size());
    u.tick(1999, true);  // still waiting out the 2 s retry
    TEST_ASSERT_EQUAL_size_t(1, t.bodies.size());
    u.tick(2000, true);  // 503
    u.tick(2000 + 3999, true);
    TEST_ASSERT_EQUAL_size_t(2, t.bodies.size());
    u.tick(2000 + 4000, true);  // 429 rate limited
    u.tick(6000 + 8000, true);  // 201
    TEST_ASSERT_TRUE(q->empty());
    TEST_ASSERT_EQUAL_UINT32(3, u.stats().failures);
    TEST_ASSERT_EQUAL_UINT32(0, u.consecutive_failures());
}

void test_retry_delay_doubles_caps_at_five_minutes_and_has_jitter() {
    TEST_ASSERT_EQUAL_UINT32(2000, Uplink::retry_delay_ms(0, 0.5f));
    TEST_ASSERT_EQUAL_UINT32(4000, Uplink::retry_delay_ms(1, 0.5f));
    TEST_ASSERT_EQUAL_UINT32(256000, Uplink::retry_delay_ms(7, 0.5f));
    TEST_ASSERT_EQUAL_UINT32(300000, Uplink::retry_delay_ms(8, 0.5f));
    TEST_ASSERT_EQUAL_UINT32(300000, Uplink::retry_delay_ms(1000, 0.5f));
    TEST_ASSERT_EQUAL_UINT32(1600, Uplink::retry_delay_ms(0, 0.0f));
    TEST_ASSERT_EQUAL_UINT32(2400, Uplink::retry_delay_ms(0, 1.0f));
}

void test_retry_timer_survives_the_millis_wraparound() {
    FakeTransport t;
    Uplink u(*q, t, half);
    q->push(L("x").c_str());
    t.answers = {0};
    const uint32_t start = UINT32_MAX - 500;  // millis() wraps after ~49.7 days
    u.tick(start, true);
    u.tick(start + 1000, true);  // wrapped, but only 1 s later: keep waiting
    TEST_ASSERT_EQUAL_size_t(1, t.bodies.size());
    u.tick(start + 2000, true);
    TEST_ASSERT_TRUE(q->empty());
}

void test_reports_made_while_offline_are_delivered_after_reconnect() {
    FakeTransport t;
    Uplink u(*q, t, half);
    for (int i = 0; i < 5; ++i) {
        char id[8];
        std::snprintf(id, sizeof id, "r%d", i);
        q->push(L(id).c_str());
        u.tick(static_cast<uint32_t>(i) * 100, false);
    }
    for (uint32_t now = 1000; now < 1010; ++now) u.tick(now, true);
    TEST_ASSERT_TRUE(q->empty());
    TEST_ASSERT_EQUAL_STRING(L("r0").c_str(), t.bodies[0].c_str());
    TEST_ASSERT_EQUAL_STRING(L("r4").c_str(), t.bodies[4].c_str());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_queue_keeps_order_and_drops_the_oldest_when_full);
    RUN_TEST(test_queue_refuses_empty_and_oversized_lines);
    RUN_TEST(test_nothing_is_sent_while_offline);
    RUN_TEST(test_stored_reports_leave_the_queue_in_order_body_unchanged);
    RUN_TEST(test_final_refusals_are_dropped_not_retried);
    RUN_TEST(test_no_answer_and_server_errors_keep_the_report_and_back_off);
    RUN_TEST(test_retry_delay_doubles_caps_at_five_minutes_and_has_jitter);
    RUN_TEST(test_retry_timer_survives_the_millis_wraparound);
    RUN_TEST(test_retry_delay_counts_from_the_end_of_a_slow_attempt);
    RUN_TEST(test_only_a_stored_ack_for_this_report_counts);
    RUN_TEST(test_json_reader_handles_escapes_and_rejects_bad_json);
    RUN_TEST(test_unknown_device_keeps_the_report_until_registered);
    RUN_TEST(test_other_403_and_signature_refusals_stay_final);
    RUN_TEST(test_success_status_without_a_valid_ack_is_retried);
    RUN_TEST(test_reports_made_while_offline_are_delivered_after_reconnect);
    return UNITY_END();
}
