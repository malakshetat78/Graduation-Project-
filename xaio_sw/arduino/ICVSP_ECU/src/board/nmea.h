#pragma once
#include <cstddef>
#include <cstdint>

#include "../core/types.h"

namespace icvsp {

// NMEA 0183 parser for the ECU's GNSS receiver over UART (u-blox NEO-6M on the current parts list; the Seeed
// L76K speaks the same sentences). Default for both: 9600 baud, 1 Hz. The talker prefix is ignored ("$GP"
// GPS-only, "$GN"/"$GB"/"$GL" multi-constellation); only RMC (time, date, position, speed, course) and GGA
// (fix quality, HDOP) are used. Every sentence must carry a correct checksum, and every field it uses is
// checked strictly: a field that is malformed or impossible is treated as missing, never guessed.
//
// Feed it every byte from the UART with the device clock; ask it for the fix at any later time.
// Between the 1 Hz sentences the UTC time is carried forward with the device clock, so every IMU
// sample gets a usable timestamp. No hardware calls: the same code runs in the native tests.
//
// The device clock is a 32-bit millisecond counter that wraps after 49.7 days. Internally it is extended
// to 64 bits on every call, so feed() or fix() must be called at least once per 49 days (the app calls
// fix() on every loop). Calls must be in time order; a timestamp up to 65 s older than the latest one is
// still placed correctly in the past (e.g. a byte stamped before a fix() call made in between).
class NmeaParser {
public:
    struct Stats {
        uint32_t sentences = 0;        // checksum OK
        uint32_t checksum_errors = 0;
        uint32_t overflows = 0;        // longer than an NMEA sentence may be
    };

    // RMC or GGA older than this makes the fix invalid: the cable is loose or the module stopped talking.
    // 2.5 s = two missed sentences at 1 Hz.
    static constexpr uint32_t kStaleMs = 2500;

    // Assumed user equivalent range error used to turn HDOP into an accuracy estimate in metres.
    // A starting guess (NEO-6M datasheet: 2.5 m CEP; L76K: 2.0 m CEP in open sky); to be checked on the road.
    static constexpr float kUereM = 2.5f;

    static constexpr uint32_t kBackTolMs = 65000;

    void feed(char c, uint32_t now_ms);
    void feed(const char* s, uint32_t now_ms) {
        while (*s) feed(*s++, now_ms);
    }

    // The fix at device time now_ms. valid = position usable; utc_ms > 0 once GNSS time is known
    // (time stays known after the fix is lost, carried forward with the device clock).
    // speed_kmh is 0 when the receiver did not give a usable speed (so the bump detector stays off).
    GnssFix fix(uint32_t now_ms) const;

    bool has_time() const { return have_utc_; }
    uint8_t satellites() const { return sats_; }
    const Stats& stats() const { return stats_; }

private:
    uint64_t clock64(uint32_t now_ms) const;  // 64-bit device time; see the class comment
    void handle(char* body, uint64_t now);
    void handle_rmc(char** f, size_t n, uint64_t now);
    void handle_gga(char** f, size_t n, uint64_t now);

    static constexpr size_t kMaxSentence = 96;  // NMEA allows 82 characters
    char buf_[kMaxSentence + 1] = {};
    size_t len_ = 0;
    bool in_sentence_ = false;
    bool overflowed_ = false;

    mutable uint32_t last_ms_ = 0;
    mutable uint64_t ext_ms_ = 0;
    mutable bool clock_started_ = false;

    bool active_ = false;  // last RMC status 'A' with a valid position
    bool ever_active_ = false;
    double lat_ = 0, lon_ = 0;
    float speed_kmh_ = 0, course_deg_ = 0, hdop_ = 99.0f;
    int quality_ = 0;
    uint8_t sats_ = 0;
    bool have_rmc_ = false, have_gga_ = false;
    uint64_t rmc_rx_ = 0, gga_rx_ = 0;  // 64-bit device time of the last RMC / GGA
    bool have_utc_ = false;
    int64_t utc_base_ms_ = 0;  // UTC of the last RMC that carried a trusted time
    uint64_t utc_base_rx_ = 0;
    Stats stats_;
};

// Days since 1970-01-01 for a civil date (proleptic Gregorian). Exposed for tests.
int64_t days_from_civil(int y, unsigned m, unsigned d);

}  // namespace icvsp
