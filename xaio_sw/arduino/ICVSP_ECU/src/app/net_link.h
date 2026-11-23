#pragma once
// The ECU network link on the board: Wi-Fi station (esp_wifi). Implements icvsp::WifiDriver, so the tested
// WifiLink state machine (join, timeout, back-off, reconnect) drives it unchanged.
// Network name and password come from secrets.h (never commit that file).
#include <atomic>

#include "../board/wifi_link.h"

class EspNetDriver : public icvsp::WifiDriver {
public:
    void begin() override;       // first call sets Wi-Fi up; later calls ask it to join again
    bool connected() override { return got_ip_.load(); }
    void disconnect() override;  // give up the current join attempt

    static const char* kind() { return "wifi"; }

private:
    void init_once();
    static void on_event(void* self, const char* base, int32_t id, void* data);

    bool started_ = false;
    std::atomic<bool> got_ip_{false};
};
