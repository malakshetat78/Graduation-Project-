// ICVSP ECU, all-in-one firmware: every part of the ECU running at the same time on the XIAO ESP32-S3 Sense.
//
//   app task (core 1)      every 5 ms: GNSS bytes -> NMEA parser; EcuApp.tick() reads the IMU (100 Hz, real MPU6050)
//                          -> detect -> fuse -> sign (Ed25519). Each signed report is copied to the BLE queue and,
//                          when Wi-Fi is set up, to the Wi-Fi queue; the app task never waits for either.
//   BLE (NimBLE)           sends each queued report to the phone (same GATT service and ACK rules as T-016).
//   delivery task (core 0) only when a Wi-Fi name is set in sdkconfig.local: Wi-Fi link + Uplink -> POST /reports.
//   camera task (core 0)   one QVGA photo every CONFIG_ICVSP_ECU_CAMERA_PERIOD_MS, checked as a JPEG. There is no
//                          hazard detection from photos on the chip yet (no AI model), so photos are not fused.
//
// Nothing is simulated in this firmware. Time and position come only from GNSS: until the L76K gives a trusted UTC
// time no report is made, and the detector ignores jolts below 8 km/h (GNSS speed), so reports need a real drive.
// Coordinates are never printed.
#include <atomic>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <sys/time.h>

#include "ble_test.h"
#include "board_pins.h"
#include "device_identity.h"
#include "ecu_app.h"
#include "esp_app_desc.h"
#include "esp_camera.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "gnss_uart.h"
#include "http_transport.h"
#include "jpeg_check.h"
#include "mpu_imu.h"
#include "net_link.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_secret_store.h"
#include "sdkconfig.h"
#include "sense_camera.h"
#include "uplink.h"
#include "wifi_link.h"

using namespace icvsp;

namespace {

constexpr uint32_t kAppPeriodMs = 5;       // app loop: IMU data-ready is polled twice per 10 ms sample
static_assert(pdMS_TO_TICKS(kAppPeriodMs) > 0, "the app period must be at least one tick: CONFIG_FREERTOS_HZ=1000 (sdkconfig.ecu)");
constexpr uint32_t kStatusEveryMs = 5000;
constexpr UBaseType_t kBleQueueDepth = 8;  // reports waiting for the phone
constexpr UBaseType_t kWifiQueueDepth = 4; // hand-off to the delivery task (which has its own 32-report queue)
constexpr bool kWifiEnabled = CONFIG_ICVSP_WIFI_SSID[0] != '\0';

struct Line {
    char s[ReportQueue::kMaxLine];
};

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
float random_unit() { return (esp_random() >> 8) * (1.0f / 16777216.0f); }

QueueHandle_t g_ble_q = nullptr;
QueueHandle_t g_wifi_q = nullptr;
std::atomic<bool> g_clock_set{false};  // system clock set from GNSS (needed for HTTPS certificate dates)
std::atomic<uint32_t> g_signed{0}, g_ble_dropped{0}, g_wifi_dropped{0};
std::atomic<uint32_t> g_cam_ok{0}, g_cam_fail{0}, g_cam_restarts{0};
std::atomic<bool> g_cam_running{false};
std::atomic<int> g_net_state{-1};  // WifiLink::State, -1 = Wi-Fi not set up
std::atomic<uint32_t> g_stored{0}, g_refused{0}, g_send_fail{0}, g_not_prov{0}, g_wifi_queued{0};
TaskHandle_t g_app_task = nullptr, g_cam_task = nullptr, g_delivery_task = nullptr;

[[noreturn]] void halt(const char* why)
{
    std::printf("ECU FAIL %s\n", why);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

// ---- reports out ---------------------------------------------------------------------------------------------
void sink(const char* line, void*)  // called by Device on the app task: copy and move on, never wait
{
    static Line l;
    const size_t n = strnlen(line, sizeof l.s);
    ++g_signed;
    if (n >= sizeof l.s) {
        ++g_ble_dropped;
        if (kWifiEnabled) ++g_wifi_dropped;
        return;
    }
    std::memcpy(l.s, line, n + 1);
    std::printf("ECU report signed #%" PRIu32 " (%u bytes) -> phone%s\n", g_signed.load(), static_cast<unsigned>(n),
                kWifiEnabled ? " + backend" : "");
    if (xQueueSend(g_ble_q, &l, 0) != pdTRUE) ++g_ble_dropped;
    if (kWifiEnabled && xQueueSend(g_wifi_q, &l, 0) != pdTRUE) ++g_wifi_dropped;
}

std::string next_ble_report()  // called by the BLE task; empty = nothing waiting
{
    static Line l;
    if (xQueueReceive(g_ble_q, &l, 0) != pdTRUE) return {};
    return std::string(l.s);
}

void delivery_task(void*)
{
    static EspNetDriver net;
    static EspHttpTransport http;
    // 35 KB: in PSRAM, to leave internal RAM to Wi-Fi, Bluetooth and the camera driver.
    void* mem = heap_caps_malloc(sizeof(ReportQueue), MALLOC_CAP_SPIRAM);
    if (mem == nullptr) halt("no PSRAM for the Wi-Fi report queue");
    ReportQueue& queue = *new (mem) ReportQueue();
    static WifiLink link(net);
    static Uplink uplink(queue, http, random_unit, now_ms);
    static Line l;
    for (;;) {
        while (xQueueReceive(g_wifi_q, &l, 0) == pdTRUE) queue.push(l.s);
        const uint32_t now = now_ms();
        link.tick(now);
        g_net_state = static_cast<int>(link.state());
        // HTTPS checks the certificate dates, so nothing is sent before the clock is set from GNSS.
        uplink.tick(now, link.online() && g_clock_set);
        const Uplink::Stats& s = uplink.stats();
        g_stored = s.stored;
        g_refused = s.refused;
        g_send_fail = s.failures;
        g_not_prov = s.not_provisioned;
        g_wifi_queued = queue.size();
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ---- camera ----------------------------------------------------------------------------------------------------
bool camera_start()
{
    SenseCameraOptions o;  // QVGA JPEG, one frame buffer in PSRAM, latest frame
    const camera_config_t cfg = sense_camera_config(o);
    for (int attempt = 1; attempt <= 3; ++attempt) {  // the T-012 runs saw a failed first start after flashing
        if (esp_camera_init(&cfg) == ESP_OK) return true;
        esp_camera_deinit();
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return false;
}

void camera_task(void*)
{
    g_cam_running = camera_start();
    std::printf(g_cam_running ? "CAM started (QVGA, one photo every %d ms)\n" : "CAM FAIL start (retrying every %d ms)\n",
                CONFIG_ICVSP_ECU_CAMERA_PERIOD_MS);
    int fails_in_row = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_ICVSP_ECU_CAMERA_PERIOD_MS));
        if (!g_cam_running) {
            g_cam_running = camera_start();
            if (g_cam_running) std::printf("CAM BACK\n");
            continue;
        }
        bool cut = false;
        camera_fb_t* fb = get_photo(&cut);
        bool ok = false;
        if (fb != nullptr) {
            ok = fb->format == PIXFORMAT_JPEG && inspect_jpeg(fb->buf, fb->len).valid();
            esp_camera_fb_return(fb);
        }
        if (ok) {
            ++g_cam_ok;
            fails_in_row = 0;
        } else {
            ++g_cam_fail;
            std::printf("CAM photo %s\n", fb == nullptr ? "missing (no frame)" : "not a valid JPEG");
            if (++fails_in_row >= 3) {  // restart the camera
                std::printf("CAM restarting after 3 failed photos\n");
                esp_camera_deinit();
                ++g_cam_restarts;
                g_cam_running = camera_start();
                fails_in_row = 0;
            }
        }
    }
}

// ---- identity ------------------------------------------------------------------------------------------------
// One higher at every start, kept in NVS (shared with the T-016 BLE firmware, so event IDs never repeat between the
// two). Only "not found" counts as the first start; any other NVS error stops the firmware instead of reusing IDs.
uint32_t next_boot_id()
{
    nvs_handle_t h;
    if (nvs_open("icvsp_ble", NVS_READWRITE, &h) != ESP_OK) return 0;
    uint32_t boot = 0;
    const esp_err_t get = nvs_get_u32(h, "boot_id", &boot);
    if ((get != ESP_OK && get != ESP_ERR_NVS_NOT_FOUND) || boot == UINT32_MAX) {
        nvs_close(h);
        return 0;
    }
    ++boot;
    const bool ok = nvs_set_u32(h, "boot_id", boot) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok ? boot : 0;
}

const char* net_text(int s)
{
    return s < 0 ? "off" : to_string(static_cast<WifiLink::State>(s));
}

uint32_t stack_left(TaskHandle_t t) { return t ? uxTaskGetStackHighWaterMark(t) : 0; }

// ---- app task ----------------------------------------------------------------------------------------------
struct AppArgs {
    Device* device;
};

void app_task(void* arg)
{
    Device& device = *static_cast<AppArgs*>(arg)->device;
    static NmeaParser gnss;
    static GnssUart uart;
    static MpuImu imu(MpuImu::Pins{IMU_I2C_PORT, IMU_SDA_GPIO, IMU_SCL_GPIO, IMU_I2C_HZ},
#if CONFIG_ICVSP_ECU_IMU_FLAT_Y_FORWARD
                      MpuImu::Mount::FlatYForward
#else
                      MpuImu::Mount::FlatXForward
#endif
    );
    static EcuApp app(device, gnss, imu);

    uart.begin(GnssUart::Pins{GPS_UART, GPS_TX_GPIO, GPS_RX_GPIO, GPS_BAUD});
    imu.begin();

    uint32_t seen_lost = 0, next_status = now_ms() + kStatusEveryMs, next_clock = 0;
    uint32_t last_samples = 0, last_used = 0, worst_loop_us = 0;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(kAppPeriodMs));
        const int64_t t0 = esp_timer_get_time();
        const uint32_t now = now_ms();
        uart.poll(gnss, now);
        if (imu.discontinuities() != seen_lost) {  // after an IMU loss or long gap the detector starts again
            seen_lost = imu.discontinuities();
            device.reset_imu();
        }
        app.tick(now);

        // System clock from GNSS, once at the first trusted time and then every minute (HTTPS needs real dates).
        const GnssFix fix = gnss.fix(now);
        if (fix.utc_ms > 0 && static_cast<int32_t>(now - next_clock) >= 0) {
            timeval tv{static_cast<time_t>(fix.utc_ms / 1000), static_cast<suseconds_t>((fix.utc_ms % 1000) * 1000)};
            settimeofday(&tv, nullptr);
            if (!g_clock_set) std::printf("ECU clock set from GNSS time\n");
            g_clock_set = true;
            next_clock = now + 60000;
        }

        const uint32_t loop_us = static_cast<uint32_t>(esp_timer_get_time() - t0);
        if (loop_us > worst_loop_us) worst_loop_us = loop_us;

        if (static_cast<int32_t>(now - next_status) >= 0) {
            next_status += kStatusEveryMs;
            const BleLinkStatus ble = ble_link_status();
            const uint32_t samples = imu.samples(), used = app.imu_samples();
            std::printf("ECU t=%" PRIu32 "s | IMU %s %" PRIu32 "/s used=%" PRIu32 "/s gaps=%" PRIu32 " clipped=%" PRIu32
                        " lost=%" PRIu32 " | GNSS bytes=%" PRIu32 " sentences=%" PRIu32 " sats=%u quality=%d fix=%s time=%s"
                        " speed=%.0fkm/h | app=%s | CAM ok=%" PRIu32 " fail=%" PRIu32 " restarts=%" PRIu32
                        " | BLE %s acked=%" PRIu32 " waiting=%u dropped=%" PRIu32 " | WIFI %s stored=%" PRIu32
                        " refused=%" PRIu32 " send_fail=%" PRIu32 " not_registered=%" PRIu32 " queued=%" PRIu32
                        " dropped=%" PRIu32 " | signed=%" PRIu32 " | RAM internal=%u min=%u block=%u psram=%u"
                        " | stack app=%" PRIu32 " cam=%" PRIu32 " net=%" PRIu32 " | worst_loop=%" PRIu32 "us\n",
                        now / 1000, imu.connected() ? "ok" : "LOST", (samples - last_samples) * 1000 / kStatusEveryMs,
                        (used - last_used) * 1000 / kStatusEveryMs, imu.gaps(), imu.clipped(), imu.lost_count(),
                        uart.bytes(), gnss.stats().sentences, static_cast<unsigned>(gnss.satellites()), gnss.fix_quality(),
                        fix.valid ? "yes" : "no", fix.utc_ms > 0 ? "yes" : "no", fix.valid ? fix.speed_kmh : 0.0f,
                        to_string(app.state()), g_cam_ok.load(), g_cam_fail.load(), g_cam_restarts.load(),
                        ble.subscribed ? "subscribed" : (ble.connected ? "connected" : "advertising"), ble.acked,
                        static_cast<unsigned>(uxQueueMessagesWaiting(g_ble_q)), g_ble_dropped.load(),
                        net_text(g_net_state), g_stored.load(), g_refused.load(), g_send_fail.load(), g_not_prov.load(),
                        g_wifi_queued.load(), g_wifi_dropped.load(), g_signed.load(),
                        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                        static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)), stack_left(g_app_task),
                        stack_left(g_cam_task), stack_left(g_delivery_task), worst_loop_us);
            last_samples = samples;
            last_used = used;
            worst_loop_us = 0;
        }
    }
}

}  // namespace

extern "C" void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(2000));  // time to open the monitor after a reset
    std::printf("\nECU BEGIN all-in-one firmware %s, ESP-IDF %s: IMU + GNSS + camera + BLE%s, nothing simulated\n",
                esp_app_get_description()->version, esp_get_idf_version(), kWifiEnabled ? " + Wi-Fi" : "");

    // NVS holds the signing key, the IMU calibration, the boot counter and the radio calibration. It is never
    // erased here: erasing would replace the key (a new identity under the same device ID).
    if (nvs_flash_init() != ESP_OK) halt("NVS could not start; not erased automatically (it holds the device key)");

    static char device_id[40];
    if (CONFIG_ICVSP_DEVICE_ID[0] != '\0') {
        std::snprintf(device_id, sizeof device_id, "%s", CONFIG_ICVSP_DEVICE_ID);
    } else {
        uint8_t mac[6];
        if (esp_efuse_mac_get_default(mac) != ESP_OK) halt("no MAC address for the device ID");
        device_id_from_mac(mac, device_id, sizeof device_id);
    }
    uint8_t seed[32];
    NvsSecretStore store;
    const KeyStatus ks = load_or_create_seed(store, esp_fill_random, seed);
    if (ks != KeyStatus::Loaded && ks != KeyStatus::Created) halt("no usable signing key");
    const uint32_t boot_id = next_boot_id();
    if (boot_id == 0) halt("boot counter in NVS unreadable: not starting, so event IDs cannot repeat");
    static Device device(device_id, boot_id, seed, sink, nullptr);  // wipes the seed
    char pk[65];
    for (int i = 0; i < 32; ++i) std::snprintf(pk + 2 * i, 3, "%02x", device.public_key()[i]);
    std::printf("ECU device=%s boot_id=%" PRIu32 " key=%s public_key=%s\n", device_id, boot_id, to_string(ks), pk);

    g_ble_q = xQueueCreateWithCaps(kBleQueueDepth, sizeof(Line), MALLOC_CAP_SPIRAM);
    if (g_ble_q == nullptr) halt("no memory for the BLE report queue");
    start_ble_link(next_ble_report);

    if (kWifiEnabled) {
        g_wifi_q = xQueueCreateWithCaps(kWifiQueueDepth, sizeof(Line), MALLOC_CAP_SPIRAM);
        if (g_wifi_q == nullptr) halt("no memory for the Wi-Fi report queue");
        if (esp_netif_init() != ESP_OK || esp_event_loop_create_default() != ESP_OK) halt("network stack start");
        std::printf("ECU Wi-Fi on, backend %s (reports sent once the clock is set from GNSS)\n", CONFIG_ICVSP_REPORT_URL);
        if (xTaskCreatePinnedToCore(delivery_task, "delivery", 12288, nullptr, 4, &g_delivery_task, 0) != pdPASS)
            halt("no memory for the delivery task");
    } else {
        std::printf("ECU Wi-Fi off (no Wi-Fi name in sdkconfig.local): reports go to the phone only\n");
    }

    if (xTaskCreatePinnedToCore(camera_task, "camera", 4096, nullptr, 2, &g_cam_task, 0) != pdPASS)
        halt("no memory for the camera task");
    static AppArgs args{&device};
    if (xTaskCreatePinnedToCore(app_task, "ecu_app", 8192, &args, 10, &g_app_task, 1) != pdPASS)
        halt("no memory for the app task");
}
