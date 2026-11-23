// ICVSP ECU report firmware (Arduino build of firmware/ecu/main/report_demo_main.cpp): the device's own code path
// from sensor samples to POST /reports, on the chip. Started from ICVSP_ECU.ino when config.h picks ICVSP_APP_REPORT.
//
//   app task       the tested EcuApp loop: GNSS NMEA -> time + position, IMU samples -> detect -> fuse ->
//                  sign (Ed25519). Each signed line is handed to the delivery task through a FreeRTOS queue
//                  and the loop moves on; it never waits for the network.
//   delivery task  the tested WifiLink (join / back-off / reconnect) and Uplink (send oldest first, remove only
//                  after the backend answered, retry with back-off) over the chip firmware's network stack (lwIP).
//
// What is real and what is simulated (stated in every log, so a demo is never mistaken for a road test):
//   real       EcuApp, Device (detection, fusion, event, signature), NmeaParser, ReportQueue, WifiLink, Uplink,
//              lwIP/TCP/HTTP on the chip, the backend's checks (known device, signature, schema, freshness).
//   simulated  the IMU samples (a 30 s drive over one pothole, from lib/icvsp_sim) and the GNSS sentences
//              (made here at the demo coordinates, ICVSP_DEMO_LAT/LON, default Cairo, carrying the current UTC time from SNTP, because the
//              GNSS receiver is not on a road). The device key is made on the chip at first boot and kept in NVS
//              (or, if chosen, a labelled demo key from the git-ignored sdkconfig.local); boot_id is the start time in seconds (no counter kept in flash yet).
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <sys/time.h>
#include <vector>

#include "../board/device_identity.h"
#include "../board/ecu_app.h"
#include "esp_app_desc.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "report_app.h"
#include "http_transport.h"
#include "net_link.h"
#include "nvs_flash.h"
#include "nvs_secret_store.h"
#include "../../config.h"
#include "log.h"
#include "../sim/sim.h"
#include "../board/uplink.h"
#include "../board/wifi_link.h"

using namespace icvsp;

namespace {

constexpr float kDriveS = 30.0f;
constexpr float kSpeedKmh = 35.0f;
constexpr float kPotholeAtS = 15.0f;
#if CONFIG_ICVSP_KEY_DEMO_SEED
constexpr char kKeyLabel[] = "demo key";
#else
constexpr char kKeyLabel[] = "per-device key";
#endif

struct Line {
    char s[ReportQueue::kMaxLine];
};

QueueHandle_t g_lines;              // app task -> delivery task
std::atomic<uint32_t> g_signed{0}, g_handoff_dropped{0};           // written by the app task
std::atomic<uint32_t> g_stored{0}, g_refused{0}, g_failures{0};     // written by the delivery task
std::atomic<uint32_t> g_not_provisioned{0};                         // 403 unknown_device answers (report kept)
std::atomic<bool> g_online{false};                                  // Wi-Fi joined with an IP address
std::atomic<bool> g_blocked_unregistered{false};                    // the last answer was 403 unknown_device
std::atomic<uint32_t> g_queue_dropped{0};  // pushed out of the full ReportQueue (oldest first)
std::atomic<size_t> g_queued{0};

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

int64_t utc_now_ms() {
    timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}

float random_unit() { return (esp_random() >> 8) * (1.0f / 16777216.0f); }

const char* build_kind() {
#if CONFIG_ICVSP_NET_QEMU_ETH
    return "EMULATOR";
#else
    return "BOARD";
#endif
}

// ---- delivery -------------------------------------------------------------------------------------------
void sink(const char* line, void*) {  // called by Device on the app task
    static Line l;
    const size_t n = strnlen(line, sizeof l.s);
    ++g_signed;
    if (n >= sizeof l.s) {
        ++g_handoff_dropped;
        return;
    }
    std::memcpy(l.s, line, n + 1);
    log_line("REPORT signed #%" PRIu32 " (%u bytes) -> delivery queue\n", g_signed.load(), static_cast<unsigned>(n));
    if (xQueueSend(g_lines, &l, 0) != pdTRUE) ++g_handoff_dropped;
}

void delivery_task(void*) {
    static EspNetDriver net;
    static EspHttpTransport http;
    static ReportQueue queue;  // RAM only for now (see report_queue.h)
    static WifiLink link(net);
    static Uplink uplink(queue, http, random_unit, now_ms);
    static Line l;
    WifiLink::State shown = WifiLink::State::Idle;
    Uplink::Stats last;
    for (;;) {
        while (xQueueReceive(g_lines, &l, 0) == pdTRUE) queue.push(l.s);
        const uint32_t now = now_ms();
        link.tick(now);
        if (link.state() != shown) {
            shown = link.state();
            log_line("NET link=%s (%s) joins=%" PRIu32 " failed=%" PRIu32 "\n", to_string(shown), EspNetDriver::kind(),
                        link.connects(), link.failed_joins());
        }
        uplink.tick(now, link.online());
        const Uplink::Stats& s = uplink.stats();
        if (s.stored != last.stored || s.refused != last.refused || s.failures != last.failures ||
            s.not_provisioned != last.not_provisioned) {
            log_line("UPLINK stored=%" PRIu32 " refused=%" PRIu32 " failures=%" PRIu32 " not_provisioned=%" PRIu32
                        " last_status=%d queued=%u queue_dropped=%" PRIu32 "%s\n", s.stored, s.refused, s.failures,
                        s.not_provisioned, s.last_status, static_cast<unsigned>(queue.size()), queue.dropped(),
                        uplink.not_provisioned() ? " (device key not registered: report kept, will retry)" : "");
            last = s;
        }
        g_stored = s.stored;
        g_refused = s.refused;
        g_failures = s.failures;
        g_not_provisioned = s.not_provisioned;
        g_online = link.online();
        g_blocked_unregistered = uplink.not_provisioned();
        g_queued = queue.size();
        g_queue_dropped = queue.dropped();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ---- simulated sensors ----------------------------------------------------------------------------------
struct ReplayImu : ImuSource {
    std::vector<ImuSample> samples;
    size_t next = 0;
    uint32_t start_ms = 0;
    bool begin() override { return true; }
    bool read(uint32_t now, ImuSample& out) override {
        if (next >= samples.size() || start_ms + samples[next].t_ms > now) return false;
        out = samples[next++];
        out.t_ms += start_ms;
        return true;
    }
    const char* name() const override { return "simulated drive (lib/icvsp_sim)"; }
};

void feed_sentence(NmeaParser& gnss, const char* body, uint32_t now) {
    unsigned sum = 0;
    for (const char* p = body; *p; ++p) sum ^= static_cast<unsigned char>(*p);
    char line[120];
    std::snprintf(line, sizeof line, "$%s*%02X\r\n", body, sum);
    gnss.feed(line, now);
}

// RMC + GGA as the L76K sends them once a second, for the whole UTC second utc_s, at the simulated position.
void feed_gnss(NmeaParser& gnss, int64_t utc_s, const GnssFix& f, uint32_t now) {
    const time_t t = static_cast<time_t>(utc_s);
    tm u;
    gmtime_r(&t, &u);
    char hms[32], date[16], la[24], lo[24], body[128];
    std::snprintf(hms, sizeof hms, "%02u%02u%02u.000", unsigned(u.tm_hour) % 24u, unsigned(u.tm_min) % 60u,
                  unsigned(u.tm_sec) % 61u);
    std::snprintf(date, sizeof date, "%02u%02u%02u", unsigned(u.tm_mday) % 32u, unsigned(u.tm_mon + 1) % 13u,
                  unsigned(u.tm_year) % 100u);
    std::snprintf(la, sizeof la, "%02d%07.4f", static_cast<int>(f.lat), (f.lat - static_cast<int>(f.lat)) * 60);
    std::snprintf(lo, sizeof lo, "%03d%07.4f", static_cast<int>(f.lon), (f.lon - static_cast<int>(f.lon)) * 60);
    std::snprintf(body, sizeof body, "GNRMC,%s,A,%s,N,%s,E,%.2f,%.1f,%s,,,A", hms, la, lo, f.speed_kmh / 1.852,
                  f.heading_deg, date);
    feed_sentence(gnss, body, now);
    std::snprintf(body, sizeof body, "GNGGA,%s,%s,N,%s,E,1,09,0.8,25.0,M,15.0,M,,", hms, la, lo);
    feed_sentence(gnss, body, now);
}

#if CONFIG_ICVSP_KEY_DEMO_SEED
int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Exactly 64 hex digits, two per byte; anything else is refused (no partial or guessed key).
bool parse_seed(const char* hex, uint8_t out[32]) {
    if (std::strlen(hex) != 64) return false;
    for (int i = 0; i < 32; ++i) {
        const int hi = hex_digit(hex[2 * i]), lo = hex_digit(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<uint8_t>(hi << 4 | lo);
    }
    return true;
}

#endif

void halt(const char* why) {
    static char what[128];
    std::snprintf(what, sizeof what, "STOPPED: %s", why);
    step(0, 8, what);
    log_line("DEMO RESULT FAIL %s\n", why);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

}  // namespace

static void report_task(void*) {
    log_line("\nDEMO BEGIN signed-report demo, %s build, firmware %s, ESP-IDF %s\n", build_kind(),
                esp_app_get_description()->version, esp_app_get_description()->idf_ver);
    log_line("DEMO link=%s url=%s\n", EspNetDriver::kind(), CONFIG_ICVSP_REPORT_URL);
    log_line("DEMO simulated: IMU drive (%.0f s, pothole at %.0f s) and GNSS sentences at demo coordinates %s, %s%s; "
                "real code: ECU app, signing, firmware network stack (in QEMU: emulated NIC), backend checks\n", kDriveS,
                kPotholeAtS, CONFIG_ICVSP_DEMO_LAT, CONFIG_ICVSP_DEMO_LON, kKeyLabel[0] == 'd' ? "; demo key" : "");

    // NVS holds the Wi-Fi calibration and this ECU's signing key. The usual ESP-IDF recipe erases NVS when it
    // reports no free pages or a new format; that would silently replace the key (a new identity under the same
    // device ID), so the firmware stops instead and a person decides (erase the flash, then register the new key).
    step(1, 8, "start flash storage (NVS) and the network stack");
    const esp_err_t err = nvs_flash_init();
    // (The Arduino core already called nvs_flash_init before setup(); calling it again is harmless.)
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
        halt("NVS needs erasing, which would destroy the device key: erase the flash and register the new key");
    if (err != ESP_OK) halt("NVS (flash storage) did not start");
    // The Arduino core may have made these already; "already done" (ESP_ERR_INVALID_STATE) is fine.
    esp_err_t e = esp_netif_init();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) halt("network stack did not start");
    e = esp_event_loop_create_default();
    if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) halt("event loop did not start");

    step(2, 8, "start the delivery task (it joins the hotspot in the background)");
    g_lines = xQueueCreate(4, sizeof(Line));
    if (g_lines == nullptr) halt("no memory for the delivery queue");
    if (xTaskCreate(delivery_task, "delivery", 12288, nullptr, 4, nullptr) != pdPASS)
        halt("no memory for the delivery task");

    // Time. On the road it comes from GNSS; here SNTP stands in for it, and the GNSS sentences carry it.
    esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_ICVSP_SNTP_SERVER);
    // First wait for Wi-Fi, then ask the time server. If no answer comes, ask again every 15 s (the first
    // request can be lost while the phone hotspot is still settling).
    step(3, 8, "wait for Wi-Fi (up to 60 s: is the hotspot ON, 2.4 GHz, name/password in secrets.h right?)");
    for (uint32_t t0 = now_ms(); !g_online && now_ms() - t0 < 60000;) vTaskDelay(pdMS_TO_TICKS(100));
    if (!g_online) halt("no Wi-Fi within 60 s: hotspot ON, 2.4 GHz, name/password in secrets.h right?");
    step(3, 8, "Wi-Fi joined; get the time from the internet (up to 90 s: does the hotspot have mobile data?)");
    esp_netif_sntp_init(&sntp);
    bool synced = false;
    for (int attempt = 1; attempt <= 6 && !synced; ++attempt) {
        if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) == ESP_OK) {
            synced = true;
        } else {
            log_line("DEMO no time yet (try %d/6, Wi-Fi %s): asking the time server again\n", attempt,
                     g_online ? "online" : "offline");
            esp_netif_sntp_start();
        }
    }
    if (!synced) halt("no internet time within 90 s although Wi-Fi joined: does the phone hotspot have mobile data?");
    const time_t boot_s = time(nullptr);
    char iso[32];
    tm u;
    gmtime_r(&boot_s, &u);
    strftime(iso, sizeof iso, "%Y-%m-%dT%H:%M:%SZ", &u);
    log_line("DEMO time synced by SNTP (%s): %s\n", CONFIG_ICVSP_SNTP_SERVER, iso);

    // This ECU's identity: the device ID from its MAC address, the signing key made on the chip at first boot
    // and kept in NVS (or the demo seed when ICVSP_KEY_DEMO_SEED is chosen). The private seed is never printed.
    step(4, 8, "device ID and signing key (load from flash, or make one on first boot)");
    static char device_id[40];
    if (CONFIG_ICVSP_DEVICE_ID[0] != '\0') {
        std::snprintf(device_id, sizeof device_id, "%s", CONFIG_ICVSP_DEVICE_ID);
    } else {
        uint8_t mac[6];
        ESP_ERROR_CHECK(esp_efuse_mac_get_default(mac));
        icvsp::device_id_from_mac(mac, device_id, sizeof device_id);
    }
    uint8_t seed[32];
#if CONFIG_ICVSP_KEY_DEMO_SEED
    if (!parse_seed(CONFIG_ICVSP_DEMO_SEED_HEX, seed)) halt("CONFIG_ICVSP_DEMO_SEED_HEX missing (sdkconfig.local)");
#else
    NvsSecretStore store;
    const icvsp::KeyStatus ks = icvsp::load_or_create_seed(store, esp_fill_random, seed);
    log_line("DEMO key %s (per-device key, NVS)\n", icvsp::to_string(ks));
    if (ks != icvsp::KeyStatus::Loaded && ks != icvsp::KeyStatus::Created) halt("no usable signing key");
#endif
    static Device* device = new Device(device_id, static_cast<uint32_t>(boot_s), seed, sink, nullptr);
    char pk[65];
    for (int i = 0; i < 32; ++i) std::snprintf(pk + 2 * i, 3, "%02x", device->public_key()[i]);
    log_line("DEMO device=%s boot_id=%" PRIu32 " public_key=%s (%s)\n", device_id,
                static_cast<uint32_t>(boot_s), pk, kKeyLabel);

    step(5, 8, "simulated 30 s drive, pothole at 15 s (detect -> sign -> hand to delivery)");
    sim::Drive drive;
    drive.duration_s = kDriveS;
    drive.speed_kmh = kSpeedKmh;
    drive.events = {{sim::Feature::Pothole, kPotholeAtS, 9.0f}};
    static ReplayImu imu;
    imu.samples = drive.imu();
    sim::FakeGnss route;
    route.lat0 = std::strtod(CONFIG_ICVSP_DEMO_LAT, nullptr);
    route.lon0 = std::strtod(CONFIG_ICVSP_DEMO_LON, nullptr);
    static NmeaParser gnss;
    static EcuApp app(*device, gnss, imu);

    int64_t last_sec = -1;
    uint32_t start = 0, next_note = 0;
    for (;;) {
        const uint32_t now = now_ms();
        const int64_t sec = utc_now_ms() / 1000;
        if (sec != last_sec) {  // a new UTC second: the receiver sends RMC + GGA
            if (start == 0) start = imu.start_ms = now;
            const float t = (now - start) / 1000.0f;
            feed_gnss(gnss, sec, route.at(t, drive.speed_at(t)), now);
            last_sec = sec;
        }
        app.tick(now);
        if (now - start >= next_note) {
            log_line("DEMO drive t=%2" PRIu32 " s state=%s imu_used=%" PRIu32 " discarded=%" PRIu32 " stale=%" PRIu32
                        " gnss_sentences=%" PRIu32 "\n", (now - start) / 1000, to_string(app.state()), app.imu_samples(),
                        app.imu_discarded(), app.imu_stale(), gnss.stats().sentences);
            next_note += 5000;
        }
        if (now - start > static_cast<uint32_t>(kDriveS * 1000) + 3000) break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    step(6, 8, "drive finished: send the last reports");
    device->flush(now_ms());
    log_line("DEMO drive done: imu_used=%" PRIu32 " stale=%" PRIu32 " signed=%" PRIu32 "\n", app.imu_samples(),
                app.imu_stale(), g_signed.load());

    // Every signed report ends as stored, refused or dropped (hand-off or full queue); wait up to 60 s for that.
    // A device the backend does not know keeps its reports and retries until it is registered, so the demo
    // stops waiting when the backend's latest answer is "unknown_device" (the uplink's current state, cleared by
    // the next stored report) and every signed report has reached the delivery queue or been settled; nothing
    // is still in the hand-off between the two tasks.
    step(7, 8, "wait for AWS answers (up to 60 s)");
    auto settled = [] { return g_stored + g_refused + g_handoff_dropped + g_queue_dropped; };
    for (int i = 0; i < 600 && (settled() < g_signed || g_queued > 0); ++i) {
        if (g_blocked_unregistered && settled() + g_queued >= g_signed) break;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    const uint32_t dropped = g_handoff_dropped + g_queue_dropped;
    step(8, 8, "finished: see the DEMO RESULT line (the delivery task keeps retrying kept reports)");
    const bool ok = g_signed > 0 && g_stored == g_signed && g_refused == 0 && dropped == 0;
    log_line("DEMO RESULT %s signed=%" PRIu32 " stored_by_backend=%" PRIu32 " refused=%" PRIu32
                " send_failures=%" PRIu32 " not_provisioned=%" PRIu32 " kept=%u dropped=%" PRIu32
                " (handoff=%" PRIu32 " queue_full=%" PRIu32 ")%s\n",
                ok ? "PASS" : "FAIL", g_signed.load(), g_stored.load(), g_refused.load(), g_failures.load(),
                g_not_provisioned.load(), static_cast<unsigned>(g_queued.load()), dropped, g_handoff_dropped.load(), g_queue_dropped.load(),
                g_blocked_unregistered && !ok ? " - register this device's public key with the backend" : "");
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

void start_report_app() {
    // The signing and JSON code needs more stack than Arduino's loop task has, so the app gets its own task.
    if (xTaskCreate(report_task, "report_app", 16384, nullptr, 5, nullptr) != pdPASS)
        log_line("DEMO RESULT FAIL no memory for the report task\n");
}

void report_status() {
    log_line("[NOW] reports signed=%" PRIu32 " stored_by_AWS=%" PRIu32 " refused=%" PRIu32 " waiting_to_send=%u%s\n",
             g_signed.load(), g_stored.load(), g_refused.load(), static_cast<unsigned>(g_queued.load()),
             g_blocked_unregistered ? " (AWS says unknown_device: board not registered yet)" : "");
}
