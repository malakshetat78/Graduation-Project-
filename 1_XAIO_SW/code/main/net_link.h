#pragma once
// The ECU's network link on the chip: implements icvsp::WifiDriver, so the tested WifiLink state machine
// (join, timeout, back-off, reconnect) drives it unchanged.
//
//   board     Wi-Fi station (esp_wifi), network name and password from CONFIG_ICVSP_WIFI_SSID / _PASSWORD,
//             which belong in the git-ignored sdkconfig.local, never in git.
//   later     a 4G modem over PPP (esp_modem) would be a third class with the same three calls.
#include <atomic>

#include "wifi_link.h"

class EspNetDriver : public icvsp::WifiDriver {
public:
    void begin() override;       // first call sets the link up; later calls ask it to join again
    bool connected() override { return got_ip_.load(); }
    void disconnect() override;  // give up the current join attempt

    static const char* kind();   // "wifi"

private:
    void init_once();
    static void on_event(void* self, const char* base, int32_t id, void* data);

    bool started_ = false;
    std::atomic<bool> got_ip_{false};
};
