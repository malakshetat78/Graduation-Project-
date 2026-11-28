#include "net_link.h"

#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_netif.h"
#include "sdkconfig.h"

#include "esp_wifi.h"

const char* EspNetDriver::kind() {
    return "wifi";
}

void EspNetDriver::on_event(void* self_ptr, const char* base, int32_t id, void* data) {
    auto* self = static_cast<EspNetDriver*>(self_ptr);
    if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        std::printf("NET lost-ip %s\n", kind());  // address gone while the link stays up: WifiLink re-joins
        self->got_ip_ = false;
        return;
    }
    if (base == IP_EVENT) {
        const auto* ev = static_cast<const ip_event_got_ip_t*>(data);
        std::printf("NET got-ip %s ip=" IPSTR " gw=" IPSTR "\n", kind(), IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.gw));
        self->got_ip_ = true;
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto* ev = static_cast<const wifi_event_sta_disconnected_t*>(data);
        std::printf("NET link-down %s reason=%d\n", kind(), ev->reason);
        self->got_ip_ = false;
    }
}

void EspNetDriver::init_once() {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_event, this));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &on_event, this));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_event, this));
    wifi_config_t cfg = {};
    std::strncpy(reinterpret_cast<char*>(cfg.sta.ssid), CONFIG_ICVSP_WIFI_SSID, sizeof cfg.sta.ssid);
    std::strncpy(reinterpret_cast<char*>(cfg.sta.password), CONFIG_ICVSP_WIFI_PASSWORD, sizeof cfg.sta.password);
    cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;  // never join an open or WEP network
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void EspNetDriver::begin() {
    if (!started_) {
        init_once();
        started_ = true;
    }
    else {
        esp_wifi_disconnect();  // a re-join (e.g. after the IP was lost while still associated) starts clean
    }
    esp_wifi_connect();
}

void EspNetDriver::disconnect() {
    if (started_) esp_wifi_disconnect();
}
