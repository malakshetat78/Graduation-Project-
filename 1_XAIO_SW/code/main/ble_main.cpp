// T-016 BLE test firmware: the BLE transport (components/icvsp_ble) sending signed test reports.
//
// Each report is a real signed report in the backend format (engine/schema/safety_event.schema.json, schema 2,
// the same envelope as the report demo and POST /reports), signed with this ECU's own Ed25519 key:
//   - device ID from the chip's MAC address, key made on the chip at first boot and kept in NVS (namespace
//     icvsp_id, shared with the report demo, so it is the same device identity). Only the public key is printed.
//   - boot_id: a counter in NVS (namespace icvsp_ble), one higher at every start, so event IDs never repeat.
// Test-only parts (this build has no sensors and no GNSS):
//   - the hazard is a test pothole (model "ble-test"), gps_valid false, no position (none is invented);
//   - the time is the build time (UTC, when CMake last configured the project) plus the time since boot. It is
//     not real time; the backend refuses reports more than 48 h old, so this is for the BLE link only.
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>

#include "ble_test.h"
#include "device_identity.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "event_builder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_secret_store.h"
#include "signer.h"
#include "types.h"

namespace {

char g_device_id[40];
uint8_t g_sk[64], g_pk[32];
icvsp::EventBuilder* g_builder = nullptr;

void fill_random(void* buf, size_t len) { esp_fill_random(buf, len); }

int64_t test_utc_ms() { return static_cast<int64_t>(ICVSP_BUILD_UTC_S) * 1000 + esp_timer_get_time() / 1000; }

// One higher at every start, kept in NVS. 0 on failure (the caller stops).
uint32_t next_boot_id()
{
    nvs_handle_t h;
    if (nvs_open("icvsp_ble", NVS_READWRITE, &h) != ESP_OK) return 0;
    uint32_t boot = 0;
    nvs_get_u32(h, "boot_id", &boot);  // absent on the first start: stays 0
    ++boot;
    const bool ok = nvs_set_u32(h, "boot_id", boot) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok ? boot : 0;
}

std::string make_signed_report()
{
    const icvsp::Detection d{icvsp::HazardType::Pothole, static_cast<uint32_t>(esp_timer_get_time() / 1000), 9.0f, 120,
                             icvsp::Severity::Medium, 0.9f};
    icvsp::GnssFix fix{};
    fix.valid = false;  // no GNSS in this build: no position
    fix.utc_ms = test_utc_ms();
    char event[640], msg_id[64];
    if (!g_builder->build(d, fix, event, sizeof event, msg_id, sizeof msg_id)) return {};
    uint8_t sig[64];
    const icvsp::MessageHeader hdr{g_device_id, msg_id, fix.utc_ms};
    if (!icvsp::sign_message(g_sk, hdr, reinterpret_cast<const uint8_t*>(event), std::strlen(event), sig)) return {};
    char sig_hex[129], pk_hex[65], line[1100];
    icvsp::to_hex(sig, 64, sig_hex);
    icvsp::to_hex(g_pk, 32, pk_hex);
    const int len = std::snprintf(line, sizeof line,
                                  "{\"event\":%s,\"sender\":\"%s\",\"msg_id\":\"%s\",\"utc_ms\":%lld,"
                                  "\"public_key\":\"%s\",\"signature\":\"%s\"}",
                                  event, g_device_id, msg_id, static_cast<long long>(fix.utc_ms), pk_hex, sig_hex);
    if (len <= 0 || static_cast<size_t>(len) >= sizeof line) return {};
    std::printf("BLE signed report %s (%d bytes)\n", msg_id, len);
    static bool shown = false;  // the first one in full, to check its signature on the laptop (no position in it)
    if (!shown) std::printf("BLE REPORT %s\n", line);
    shown = true;
    return std::string(line, static_cast<size_t>(len));
}

void stop(const char* why)
{
    std::printf("BLE FAIL %s\n", why);
    for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
}

}  // namespace

extern "C" void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(2000));  // time to open the monitor after a reset

    // NVS holds the Bluetooth calibration, this ECU's signing key and the boot counter. It is never erased here:
    // erasing would replace the key (a new identity under the same device ID).
    const esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) stop("NVS could not start and is not erased automatically (it holds the device key)");

    uint8_t mac[6];
    if (esp_efuse_mac_get_default(mac) != ESP_OK) stop("no MAC address for the device ID");
    icvsp::device_id_from_mac(mac, g_device_id, sizeof g_device_id);

    uint8_t seed[32];
    NvsSecretStore store;
    const icvsp::KeyStatus ks = icvsp::load_or_create_seed(store, fill_random, seed);
    std::printf("BLE key %s (per-device key, NVS)\n", icvsp::to_string(ks));
    if (ks != icvsp::KeyStatus::Loaded && ks != icvsp::KeyStatus::Created) stop("no usable signing key");
    icvsp::keypair_from_seed(seed, g_sk, g_pk);  // wipes the seed

    const uint32_t boot_id = next_boot_id();
    if (boot_id == 0) stop("could not keep the boot counter in NVS");
    static icvsp::EventBuilder builder(g_device_id, "ble-test", boot_id);
    g_builder = &builder;

    char pk_hex[65];
    icvsp::to_hex(g_pk, 32, pk_hex);
    std::printf("BLE device=%s boot_id=%" PRIu32 " public_key=%s\n", g_device_id, boot_id, pk_hex);
    std::printf("BLE test reports: pothole, no position (gps_valid false), time = build time + uptime (not real)\n");

    start_ble_test(make_signed_report);  // starts its own tasks
}
