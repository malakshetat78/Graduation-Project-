#include <cinttypes>
#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "net_link.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

extern "C" void app_main() {
    constexpr auto ssid_length = sizeof(CONFIG_ICVSP_WIFI_SSID) - 1;
    constexpr auto password_length = sizeof(CONFIG_ICVSP_WIFI_PASSWORD) - 1;
    if (ssid_length == 0 || ssid_length > 32 || password_length < 8 ||
        password_length > 63 || std::strcmp(CONFIG_ICVSP_WIFI_SSID, "ICVSP_TEST_HOTSPOT") == 0) {
        std::puts("HOTSPOT CONFIG_REQUIRED: set a WPA2 hotspot SSID and passphrase in sdkconfig.local");
        return;
    }

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    static EspNetDriver driver;
    icvsp::WifiLink link(driver);
    auto last = link.state();
    std::puts("HOTSPOT READY");
    for (;;) {
        link.tick(static_cast<uint32_t>(esp_timer_get_time() / 1000));
        if (link.state() != last) {
            last = link.state();
            std::printf("HOTSPOT state=%s joins=%" PRIu32 " failed=%" PRIu32 "\n",
                        icvsp::to_string(last), link.connects(), link.failed_joins());
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
