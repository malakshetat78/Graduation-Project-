#pragma once
#include <cstdint>

namespace icvsp {

// What a network link driver must provide. Named after Wi-Fi, but any link that gives the ECU an IP address
// fits: Wi-Fi station on the board, the emulated Ethernet port in QEMU, a 4G modem (PPP) later; tests: a fake.
class WifiDriver {
public:
    virtual ~WifiDriver() = default;
    virtual void begin() = 0;       // start joining the configured network (returns at once)
    virtual bool connected() = 0;   // associated and has an IP address
    virtual void disconnect() = 0;  // give up the current attempt
};

// Keeps the device on Wi-Fi: join, wait up to 15 s, and if that fails wait and try again
// (5 s, 10 s, 20 s ... up to 60 s). A lost connection is retried after 1 s.
// Report delivery only runs while online(); detection and queueing never wait for the network.
class WifiLink {
public:
    enum class State { Idle, Connecting, Online, Backoff };

    static constexpr uint32_t kJoinTimeoutMs = 15000;
    static constexpr uint32_t kFirstBackoffMs = 5000;
    static constexpr uint32_t kMaxBackoffMs = 60000;
    static constexpr uint32_t kReconnectAfterLossMs = 1000;

    explicit WifiLink(WifiDriver& driver) : driver_(driver) {}

    void tick(uint32_t now_ms);
    bool online() const { return state_ == State::Online; }
    State state() const { return state_; }
    uint32_t connects() const { return connects_; }  // successful joins (1 = first, more = reconnects)
    uint32_t failed_joins() const { return failed_; }

private:
    void start_join(uint32_t now_ms);
    void back_off(uint32_t now_ms, uint32_t delay_ms);

    WifiDriver& driver_;
    State state_ = State::Idle;
    uint32_t since_ms_ = 0, wait_ms_ = 0;
    uint32_t next_backoff_ms_ = kFirstBackoffMs;
    uint32_t connects_ = 0, failed_ = 0;
};

const char* to_string(WifiLink::State s);

}  // namespace icvsp
