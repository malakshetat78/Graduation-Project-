// Wi-Fi join / reconnect state machine (board layer), against a fake driver.
#include <unity.h>

#include "wifi_link.h"

using namespace icvsp;

namespace {
struct FakeWifi : WifiDriver {
    bool up = false;
    int begins = 0, disconnects = 0;
    void begin() override { ++begins; }
    bool connected() override { return up; }
    void disconnect() override { ++disconnects; }
};
}  // namespace

void setUp() {}
void tearDown() {}

void test_joins_and_goes_online() {
    FakeWifi w;
    WifiLink link(w);
    link.tick(0);
    TEST_ASSERT_EQUAL(WifiLink::State::Connecting, link.state());
    TEST_ASSERT_EQUAL_INT(1, w.begins);
    w.up = true;
    link.tick(3000);
    TEST_ASSERT_TRUE(link.online());
    TEST_ASSERT_EQUAL_UINT32(1, link.connects());
}

void test_failed_join_backs_off_5_10_20_up_to_60_seconds() {
    FakeWifi w;
    WifiLink link(w);
    uint32_t t = 0;
    link.tick(t);
    const uint32_t expected[] = {5000, 10000, 20000, 40000, 60000, 60000};
    for (uint32_t wait : expected) {
        t += WifiLink::kJoinTimeoutMs;
        link.tick(t);
        TEST_ASSERT_EQUAL(WifiLink::State::Backoff, link.state());
        link.tick(t + wait - 1);
        TEST_ASSERT_EQUAL(WifiLink::State::Backoff, link.state());
        t += wait;
        link.tick(t);
        TEST_ASSERT_EQUAL(WifiLink::State::Connecting, link.state());
    }
    TEST_ASSERT_EQUAL_UINT32(6, link.failed_joins());
    TEST_ASSERT_EQUAL_INT(6, w.disconnects);
    TEST_ASSERT_EQUAL_INT(7, w.begins);
}

void test_lost_connection_reconnects_after_one_second_and_resets_backoff() {
    FakeWifi w;
    WifiLink link(w);
    link.tick(0);
    link.tick(WifiLink::kJoinTimeoutMs);  // one failure: next back-off would be 10 s
    link.tick(WifiLink::kJoinTimeoutMs + 5000);
    w.up = true;
    link.tick(WifiLink::kJoinTimeoutMs + 6000);
    TEST_ASSERT_TRUE(link.online());
    w.up = false;  // router restarted
    link.tick(100000);
    TEST_ASSERT_EQUAL(WifiLink::State::Backoff, link.state());
    link.tick(100000 + WifiLink::kReconnectAfterLossMs);
    TEST_ASSERT_EQUAL(WifiLink::State::Connecting, link.state());
    link.tick(100000 + WifiLink::kReconnectAfterLossMs + WifiLink::kJoinTimeoutMs);
    link.tick(100000 + WifiLink::kReconnectAfterLossMs + WifiLink::kJoinTimeoutMs + 4999);
    TEST_ASSERT_EQUAL(WifiLink::State::Backoff, link.state());  // back to the first 5 s step
    link.tick(100000 + WifiLink::kReconnectAfterLossMs + WifiLink::kJoinTimeoutMs + 5000);
    TEST_ASSERT_EQUAL(WifiLink::State::Connecting, link.state());
    w.up = true;
    link.tick(200000);
    TEST_ASSERT_EQUAL_UINT32(2, link.connects());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_joins_and_goes_online);
    RUN_TEST(test_failed_join_backs_off_5_10_20_up_to_60_seconds);
    RUN_TEST(test_lost_connection_reconnects_after_one_second_and_resets_backoff);
    return UNITY_END();
}
