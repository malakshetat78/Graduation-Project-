// Offline queue + delivery to POST /reports with retry (board layer), against a fake HTTPS client.
#include <unity.h>

#include <cstdio>
#include <string>
#include <vector>

#include "report_queue.h"
#include "uplink.h"

using namespace icvsp;

namespace {

struct FakeTransport : ReportTransport {
    std::vector<int> answers;  // consumed front to back; 201 once empty
    std::vector<std::string> bodies;
    int post_report(const char* line, size_t len) override {
        bodies.emplace_back(line, len);
        if (answers.empty()) return 201;
        const int a = answers.front();
        answers.erase(answers.begin());
        return a;
    }
};

float half() { return 0.5f; }

uint32_t g_clock = 0;  // device clock for the slow-transport test
uint32_t clock_ms() { return g_clock; }

struct SlowTransport : ReportTransport {  // every POST takes 10 s and gets no answer (e.g. a TCP timeout)
    int posts = 0;
    int post_report(const char*, size_t) override {
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
    q->push("a");
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

void test_nothing_is_sent_while_offline() {
    FakeTransport t;
    Uplink u(*q, t, half);
    q->push("a");
    u.tick(0, false);
    TEST_ASSERT_EQUAL_size_t(0, t.bodies.size());
    TEST_ASSERT_EQUAL_size_t(1, q->size());
}

void test_stored_reports_leave_the_queue_in_order_body_unchanged() {
    FakeTransport t;
    Uplink u(*q, t, half);
    const char* line = "{\"event\":{\"a\":1},\"sender\":\"car\",\"signature\":\"ab\"}";
    q->push(line);
    q->push("second");
    t.answers = {201, 200};  // accepted, then duplicate (already stored)
    u.tick(0, true);
    u.tick(1, true);
    TEST_ASSERT_EQUAL_size_t(2, t.bodies.size());
    TEST_ASSERT_EQUAL_STRING(line, t.bodies[0].c_str());  // the signed bytes go out exactly as built
    TEST_ASSERT_EQUAL_STRING("second", t.bodies[1].c_str());
    TEST_ASSERT_TRUE(q->empty());
    TEST_ASSERT_EQUAL_UINT32(2, u.stats().stored);
}

void test_final_refusals_are_dropped_not_retried() {
    const int finals[] = {400, 401, 403, 409, 413, 422};
    for (int status : finals) {
        FakeTransport t;
        Uplink u(*q, t, half);
        q->push("x");
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
    q->push("x");
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
    q->push("x");
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
        char line[8];
        std::snprintf(line, sizeof line, "r%d", i);
        q->push(line);
        u.tick(static_cast<uint32_t>(i) * 100, false);
    }
    for (uint32_t now = 1000; now < 1010; ++now) u.tick(now, true);
    TEST_ASSERT_TRUE(q->empty());
    TEST_ASSERT_EQUAL_STRING("r0", t.bodies[0].c_str());
    TEST_ASSERT_EQUAL_STRING("r4", t.bodies[4].c_str());
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
    RUN_TEST(test_reports_made_while_offline_are_delivered_after_reconnect);
    return UNITY_END();
}
