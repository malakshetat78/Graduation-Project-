// The whole ECU loop with fake hardware: NMEA bytes as the GNSS receiver (NEO-6M or L76K) sends them, a simulated IMU drive
// over a pothole, Wi-Fi that comes up late, and a fake HTTPS client. Checks that a correctly signed report
// reaches POST /reports and nothing is reported before GNSS time is known.
#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "ecu_app.h"
#include "json_reply.h"
#include "uplink.h"
#include "wifi_link.h"
#include "signer.h"
#include "sim.h"

using namespace icvsp;

namespace {

struct ReplayImu : ImuSource {
    std::vector<ImuSample> samples;
    size_t next = 0;
    bool begin() override { return true; }
    bool read(uint32_t now_ms, ImuSample& out) override {
        if (next >= samples.size() || samples[next].t_ms > now_ms) return false;
        out = samples[next++];
        return true;
    }
    const char* name() const override { return "replay"; }
};

struct ListImu : ImuSource {  // hands out its samples in order, whatever their time stamps
    std::vector<ImuSample> samples;
    size_t next = 0;
    bool begin() override { return true; }
    bool read(uint32_t, ImuSample& out) override {
        if (next >= samples.size()) return false;
        out = samples[next++];
        return true;
    }
    const char* name() const override { return "list"; }
};

struct FakeWifi : WifiDriver {
    bool up = false;
    void begin() override {}
    bool connected() override { return up; }
    void disconnect() override {}
};

struct FakeTransport : ReportTransport {  // answers like the backend: a stored ACK for the report it got
    std::vector<std::string> bodies;
    int post_report(const char* line, size_t len, char* reply, size_t cap) override {
        bodies.emplace_back(line, len);
        char id[160] = {}, sender[80] = {};
        json_top_string(bodies.back().c_str(), "msg_id", id, sizeof id);
        json_top_string(bodies.back().c_str(), "sender", sender, sizeof sender);
        std::snprintf(reply, cap, "{\"status\":\"accepted\",\"ack\":\"stored\",\"event_id\":\"%s\",\"device_id\":\"%s\"}",
                      id, sender);
        return 201;
    }
};

float half() { return 0.5f; }

std::string nmea(const std::string& body) {
    unsigned sum = 0;
    for (char c : body) sum ^= static_cast<unsigned char>(c);
    char tail[8];
    std::snprintf(tail, sizeof tail, "*%02X\r\n", sum);
    return "$" + body + tail;
}

// RMC + GGA for a car at (lat, lon) going east, at UTC 2026-11-15 10:00:00 + t_s
std::string sentences_at(int t_s, double lat, double lon, float speed_kmh) {
    const int secs = 10 * 3600 + t_s;
    char hms[16], la[16], lo[16], buf[200];
    std::snprintf(hms, sizeof hms, "%02d%02d%02d.000", secs / 3600, secs / 60 % 60, secs % 60);
    std::snprintf(la, sizeof la, "%02d%07.4f", static_cast<int>(lat), (lat - static_cast<int>(lat)) * 60);
    std::snprintf(lo, sizeof lo, "%03d%07.4f", static_cast<int>(lon), (lon - static_cast<int>(lon)) * 60);
    std::snprintf(buf, sizeof buf, "GNRMC,%s,A,%s,N,%s,E,%.2f,90.0,151126,,,A", hms, la, lo, speed_kmh / 1.852);
    std::string out = nmea(buf);
    std::snprintf(buf, sizeof buf, "GNGGA,%s,%s,N,%s,E,1,09,0.8,25.0,M,15.0,M,,", hms, la, lo);
    return out + nmea(buf);
}

std::string field(const std::string& line, const char* key) {  // value of a top-level string/number field
    const std::string k = std::string("\"") + key + "\":";
    size_t p = line.rfind(k);
    if (p == std::string::npos) return "";
    p += k.size();
    if (line[p] == '"') {
        const size_t e = line.find('"', p + 1);
        return line.substr(p + 1, e - p - 1);
    }
    const size_t e = line.find_first_of(",}", p);
    return line.substr(p, e - p);
}

struct Rig {
    ReportQueue queue;
    NmeaParser gnss;
    ReplayImu imu;
    FakeWifi wifi_hw;
    FakeTransport https;
    WifiLink wifi{wifi_hw};
    Uplink uplink{queue, https, half};
    uint8_t seed[32];
    Device* device;
    EcuApp* app;
    uint8_t pk[32];

    Rig() {
        std::memset(seed, 0x11, sizeof seed);
        device = new Device("ecu-test-01", 7, seed, EcuApp::queue_sink, &queue);
        std::memcpy(pk, device->public_key(), 32);
        app = new EcuApp(*device, gnss, imu);
    }
    ~Rig() {
        delete app;
        delete device;
    }
};

// The delivery loop (its own task on the board): keep the link up and send queued reports.
template <typename R>
void deliver(R& r, uint32_t now) {
    r.wifi.tick(now);
    r.uplink.tick(now, r.wifi.online());
}

// Drive 30 s east at 35 km/h with a pothole at 15 s. GNSS gets its first fix at 3 s, Wi-Fi only at 20 s.
void drive(Rig& r, bool gnss_ever) {
    sim::Drive d;
    d.duration_s = 30;
    d.speed_kmh = 35;
    d.events = {{sim::Feature::Pothole, 15.0f, 9.0f}};
    r.imu.samples = d.imu();
    double lon = 31.2800;
    for (uint32_t now = 0; now <= 30000; now += 10) {
        if (now % 1000 == 0) {
            const int t = static_cast<int>(now / 1000);
            if (gnss_ever && t >= 3) r.gnss.feed(sentences_at(t, 30.0444, lon, d.speed_at(t)).c_str(), now);
            lon += d.speed_at(t) / 3.6 / (111320.0 * std::cos(30.0444 * 3.14159265 / 180));
        }
        r.wifi_hw.up = now >= 20000;
        r.app->tick(now);
        deliver(r, now);
    }
}

}  // namespace

void setUp() {}
void tearDown() {}

void test_waits_for_gnss_time_before_using_the_imu() {
    Rig r;
    r.app->tick(0);
    TEST_ASSERT_EQUAL(EcuApp::State::WaitingForTime, r.app->state());
    drive(r, false);  // GNSS never gets a fix (antenna unplugged)
    TEST_ASSERT_EQUAL(EcuApp::State::WaitingForTime, r.app->state());
    TEST_ASSERT_EQUAL_UINT32(0, r.app->imu_samples());
    TEST_ASSERT_TRUE(r.app->imu_discarded() > 2900);
    TEST_ASSERT_EQUAL_size_t(0, r.https.bodies.size());  // nothing is invented or sent without a time
}

void test_pothole_report_is_signed_queued_while_offline_and_delivered_later() {
    Rig r;
    drive(r, true);
    TEST_ASSERT_EQUAL(EcuApp::State::Running, r.app->state());
    TEST_ASSERT_TRUE(r.app->imu_discarded() > 0);  // the first 3 s, before the fix
    TEST_ASSERT_TRUE(r.app->imu_samples() > 2000);
    TEST_ASSERT_EQUAL_size_t(1, r.https.bodies.size());
    TEST_ASSERT_EQUAL_size_t(0, r.queue.size());
    TEST_ASSERT_EQUAL_UINT32(1, r.uplink.stats().stored);

    const std::string& line = r.https.bodies[0];
    TEST_ASSERT_NOT_EQUAL(std::string::npos, line.find("\"pothole\""));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, line.find("\"gps_valid\":true"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, line.find("\"latitude\":30.0444"));

    // The signature checks out over the exact event bytes, sender, msg_id and GNSS time in the line.
    const size_t a = line.find("{\"event\":") + 9, b = line.find(",\"sender\":");
    const std::string event = line.substr(a, b - a);
    const std::string sender = field(line, "sender"), msg_id = field(line, "msg_id");
    const long long utc = std::atoll(field(line, "utc_ms").c_str());
    TEST_ASSERT_EQUAL_STRING("ecu-test-01", sender.c_str());
    TEST_ASSERT_TRUE(utc > 1794736800000LL && utc < 1794736830000LL);  // 10:00:00 .. 10:00:30 UTC
    uint8_t sig[64];
    TEST_ASSERT_TRUE(from_hex(field(line, "signature").c_str(), sig, 64));
    const MessageHeader h{sender.c_str(), msg_id.c_str(), utc};
    TEST_ASSERT_TRUE(verify_message(r.pk, h, reinterpret_cast<const uint8_t*>(event.data()), event.size(), sig));
}

void test_old_and_pre_time_imu_samples_are_dropped() {
    Rig r;
    r.imu.samples = {{500, 0, 0, 9.81f},    // taken before GNSS time was known, read late
                     {1500, 0, 0, 9.81f},   // 1.5 s old when read (the loop stalled on a slow send)
                     {2950, 0, 0, 9.81f}};  // fresh
    r.gnss.feed(sentences_at(0, 30.0444, 31.28, 36).c_str(), 1000);
    r.app->tick(1000);   // time becomes known; sample 500 is older than that
    r.app->tick(1001);
    r.app->tick(3000);   // reads 1500 (stale) and 2950 (fresh)
    TEST_ASSERT_EQUAL(EcuApp::State::Running, r.app->state());
    TEST_ASSERT_EQUAL_UINT32(1, r.app->imu_samples());
    TEST_ASSERT_EQUAL_UINT32(1, r.app->imu_discarded());
    TEST_ASSERT_EQUAL_UINT32(1, r.app->imu_stale());
    // GNSS time 10:00:00.000 was read at device time 1000; the used sample was taken at 2950
    TEST_ASSERT_EQUAL_INT64(1794736800000LL + 1950, r.app->last_sample_utc_ms());
}

struct ListRig {  // the same parts as Rig, with an IMU that ignores time stamps
    ReportQueue queue;
    NmeaParser gnss;
    ListImu imu;
    FakeWifi wifi_hw;
    FakeTransport https;
    WifiLink wifi{wifi_hw};
    Uplink uplink{queue, https, half};
    uint8_t seed[32];
    Device* device;
    EcuApp* app;
    ListRig() {
        std::memset(seed, 0x22, sizeof seed);
        device = new Device("ecu-test-02", 1, seed, EcuApp::queue_sink, &queue);
        app = new EcuApp(*device, gnss, imu);
    }
    ~ListRig() {
        delete app;
        delete device;
    }
};

void test_sample_age_limit_is_exact_and_future_samples_are_refused() {
    ListRig r;
    r.gnss.feed(sentences_at(0, 30.0444, 31.28, 36).c_str(), 1000);
    r.app->tick(1000);
    r.app->tick(2000);  // past the start-up window
    const uint32_t now = 5000;
    r.imu.samples = {{now - EcuApp::kMaxSampleAgeMs, 0, 0, 9.81f},       // exactly at the limit: used
                     {now - EcuApp::kMaxSampleAgeMs - 1, 0, 0, 9.81f},   // 1 ms too old: stale
                     {now + 5, 0, 0, 9.81f}};                            // from the future: refused
    r.gnss.feed(sentences_at(4, 30.0444, 31.28, 36).c_str(), now);
    r.app->tick(now);
    TEST_ASSERT_EQUAL_UINT32(1, r.app->imu_samples());
    TEST_ASSERT_EQUAL_UINT32(2, r.app->imu_stale());
}

void test_samples_are_still_used_after_25_days_of_running() {
    ListRig r;
    r.gnss.feed(sentences_at(0, 30.0444, 31.28, 36).c_str(), 1000);
    r.app->tick(1000);
    r.app->tick(1300);
    const uint32_t later = 1000u + 0x80000000u + 100u;  // 24.9 days on: an int32 time difference flips sign here
    r.imu.samples = {{later - 10, 0, 0, 9.81f}};
    r.app->tick(later);
    TEST_ASSERT_EQUAL_UINT32(1, r.app->imu_samples());
    TEST_ASSERT_EQUAL_UINT32(0, r.app->imu_discarded());
}

void test_app_loop_never_touches_the_network() {
    // Sensing and sending are separate loops: however long a send takes, the IMU keeps being read.
    Rig r;
    r.wifi_hw.up = true;
    sim::Drive d;
    d.duration_s = 30;
    d.speed_kmh = 35;
    d.events = {{sim::Feature::Pothole, 15.0f, 9.0f}};
    r.imu.samples = d.imu();
    double lon = 31.2800;
    for (uint32_t now = 0; now <= 30000; now += 10) {
        if (now % 1000 == 0) {
            const int t = static_cast<int>(now / 1000);
            r.gnss.feed(sentences_at(t, 30.0444, lon, d.speed_at(t)).c_str(), now);
            lon += d.speed_at(t) / 3.6 / (111320.0 * std::cos(30.0444 * 3.14159265 / 180));
        }
        r.app->tick(now);  // the app loop only: no deliver()
    }
    TEST_ASSERT_EQUAL_size_t(1, r.queue.size());       // the pothole was detected, signed and queued
    TEST_ASSERT_EQUAL_size_t(0, r.https.bodies.size());  // but nothing was sent from the app loop
    for (uint32_t now = 30010; now <= 30100; now += 10) deliver(r, now);  // join, then send
    TEST_ASSERT_EQUAL_size_t(1, r.https.bodies.size());  // the delivery loop sends it
    TEST_ASSERT_EQUAL_size_t(0, r.queue.size());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_waits_for_gnss_time_before_using_the_imu);
    RUN_TEST(test_pothole_report_is_signed_queued_while_offline_and_delivered_later);
    RUN_TEST(test_old_and_pre_time_imu_samples_are_dropped);
    RUN_TEST(test_sample_age_limit_is_exact_and_future_samples_are_refused);
    RUN_TEST(test_samples_are_still_used_after_25_days_of_running);
    RUN_TEST(test_app_loop_never_touches_the_network);
    return UNITY_END();
}
