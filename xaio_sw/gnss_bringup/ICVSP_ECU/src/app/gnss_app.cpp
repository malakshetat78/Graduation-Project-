// T-014: Seeed L76K GNSS hardware bring-up. No synthetic positions or internet required.
// Existing checked NMEA parser is reused; IMU/camera/BLE code remains unchanged.
#include "gnss_app.h"

#include <Arduino.h>
#include <time.h>

#include "../../config.h"
#include "../board/nmea.h"
#include "log.h"

namespace {

icvsp::NmeaParser parser;
uint32_t start_ms = 0;
uint32_t last_byte_ms = 0;
uint32_t last_valid_nmea_ms = 0;
uint32_t last_print_ms = 0;
uint32_t bytes_seen = 0;
uint32_t last_good_count = 0;
bool got_any_byte = false;
bool got_valid_nmea = false;

enum class Status { STARTING, WAITING_UART, NO_VALID_NMEA, WAITING_RMC_GGA, DISCONNECTED, STALE, NO_FIX, FIX };
Status last_status = Status::STARTING;

const char* label(Status state) {
    switch (state) {
        case Status::WAITING_UART: return "WAITING_UART";
        case Status::NO_VALID_NMEA: return "NO_VALID_NMEA";
        case Status::WAITING_RMC_GGA: return "WAITING_RMC_GGA";
        case Status::DISCONNECTED: return "DISCONNECTED";
        case Status::STALE: return "STALE";
        case Status::NO_FIX: return "NO_FIX";
        case Status::FIX: return "VALID_FIX";
        default: return "STARTING";
    }
}

// The receiver can send valid sentences indoors while it has no satellite fix.
// Only an up-to-date RMC+GGA pair with a valid status/quality is a valid fix.
Status get_status(uint32_t now_ms, const icvsp::GnssFix& fix) {
    if (got_any_byte && now_ms - last_byte_ms > 3500) return Status::DISCONNECTED;
    if (!got_any_byte) return now_ms - start_ms < 5000 ? Status::WAITING_UART : Status::DISCONNECTED;
    if (!got_valid_nmea) return Status::NO_VALID_NMEA;
    if (now_ms - last_valid_nmea_ms > 3500) return Status::STALE;
    if (!parser.has_position_sentences()) return Status::WAITING_RMC_GGA;
    if (!parser.position_sentences_fresh(now_ms)) return Status::STALE;
    return fix.valid ? Status::FIX : Status::NO_FIX;
}

#if CONFIG_ICVSP_GNSS_SOFTWARE_TESTS
// Drain the real UART even during injection so buffered old fixes cannot recover it.
enum class Test { NONE, STALE, DISCONNECT };
Test test = Test::NONE;
uint32_t test_started_ms = 0;
bool recovering = false;
bool saw_expected = false;
bool wait_for_sentence = false;

void test_commands(uint32_t now) {
    for (int n = 0; n < 32 && Serial.available() > 0; ++n) {
        const int ch = Serial.read();
        if (ch != 'S' && ch != 'D') continue;
        if (test != Test::NONE) {
            log_line("GNSS TEST BUSY\n");
            continue;
        }
        if (get_status(now, parser.fix(now)) != Status::FIX) {
            log_line("GNSS TEST SKIP: need a current VALID_FIX\n");
            continue;
        }
        test = ch == 'S' ? Test::STALE : Test::DISCONNECT;
        test_started_ms = now;
        recovering = false;
        saw_expected = false;
        wait_for_sentence = true;
        log_line("GNSS TEST %c BEGIN software simulation 6000ms\n", ch);
    }
    if (test != Test::NONE && !recovering && now - test_started_ms >= 6000) {
        recovering = true;
        wait_for_sentence = true;
        log_line("GNSS TEST timeout %s; waiting for fresh RMC+GGA\n",
                 saw_expected ? "PASS" : "FAIL");
    }
}

void test_observe(uint32_t now, Status state) {
    if (test == Test::NONE) return;
    const Status expected = test == Test::STALE ? Status::STALE : Status::DISCONNECTED;
    if (!recovering && state == expected) saw_expected = true;
    if (recovering && state == Status::FIX) {
        log_line("GNSS TEST %c %s: fresh fix recovered\n",
                 test == Test::STALE ? 'S' : 'D', saw_expected ? "PASS" : "FAIL");
        test = Test::NONE;
    } else if (recovering && now - test_started_ms >= 21000) {
        log_line("GNSS TEST FAIL: no valid recovery within 15000ms\n");
        test = Test::NONE;
    }
}
#endif

void get_utc(char* out, size_t out_sz, int64_t utc_ms) {
    if (utc_ms <= 0) {
        snprintf(out, out_sz, "unknown (wait for valid fix)");
        return;
    }
    const time_t epoch_s = static_cast<time_t>(utc_ms / 1000);
    struct tm utc_tm = {};
    if (gmtime_r(&epoch_s, &utc_tm) == nullptr ||
        strftime(out, out_sz, "%Y-%m-%dT%H:%M:%SZ", &utc_tm) == 0) {
        snprintf(out, out_sz, "unknown");
    }
}

}  // namespace

void start_gnss_app() {
    start_ms = millis();
    step(1, 3, "GNSS: initialize UART from config.h");
    Serial1.begin(CONFIG_ICVSP_GNSS_BAUD, SERIAL_8N1,
                  CONFIG_ICVSP_GNSS_RX_PIN, CONFIG_ICVSP_GNSS_TX_PIN);
    log_line("\nGNSS BEGIN T-014: Seeed L76K hardware bring-up\n");
    log_line("GNSS UART baud=%lu RX=GPIO%d(D7) TX=GPIO%d(D6)\n",
             (unsigned long)CONFIG_ICVSP_GNSS_BAUD,
             CONFIG_ICVSP_GNSS_RX_PIN, CONFIG_ICVSP_GNSS_TX_PIN);
    log_line("GNSS INFO module TX -> ECU RX, module RX <- ECU TX; NO WIFI / NO CLOUD\n");
    log_line("GNSS INFO indoor NO_FIX is expected; test outdoors with clear sky\n");
    step(2, 3, "GNSS: live NMEA, fix, staleness, disconnect/recovery");
#if CONFIG_ICVSP_GNSS_SOFTWARE_TESTS
    log_line("GNSS TEST enabled: S=stale, D=disconnect; start from VALID_FIX\n");
#endif
}

void gnss_app_loop() {
    uint32_t now = millis();
#if CONFIG_ICVSP_GNSS_SOFTWARE_TESTS
    test_commands(now);
#endif
    // Drain input on every loop, to prevent overflowing the UART receive buffer.
    // 512 bytes per iteration still leaves room for the idle task.
    for (int n = 0; n < 512 && Serial1.available() > 0; ++n) {
        const int ch = Serial1.read();
        if (ch < 0) break;
        now = millis();
#if CONFIG_ICVSP_GNSS_SOFTWARE_TESTS
        if (test == Test::DISCONNECT && !recovering) continue;
#endif
        got_any_byte = true;
        last_byte_ms = now;
        ++bytes_seen;
#if CONFIG_ICVSP_GNSS_SOFTWARE_TESTS
        if (test == Test::STALE && !recovering) continue;
        if (wait_for_sentence) {
            if (ch != '$') continue;
            wait_for_sentence = false;
        }
#endif
        parser.feed(static_cast<char>(ch), now);
        const uint32_t good = parser.stats().sentences;
        if (good != last_good_count) {
            last_good_count = good;
            last_valid_nmea_ms = now;
            got_valid_nmea = true;
        }
    }

    now = millis();
    const icvsp::GnssFix fix = parser.fix(now);
    const Status state = get_status(now, fix);
#if CONFIG_ICVSP_GNSS_SOFTWARE_TESTS
    test_observe(now, state);
#endif
    if (state != last_status) {
        log_line("GNSS EVENT %s -> %s\n", label(last_status), label(state));
        last_status = state;
    }
    if (now - last_print_ms < CONFIG_ICVSP_GNSS_PRINT_EVERY_MS) return;
    last_print_ms = now;

    const icvsp::NmeaParser::Stats& stats = parser.stats();
    const float hdop = parser.hdop();
    if (state == Status::FIX) {
        char utc[40];
        get_utc(utc, sizeof utc, fix.utc_ms);
        log_line("GNSS FIX lat=%.7f lon=%.7f UTC=%s sats=%u HDOP=%.2f quality=%d speed=%.1fkm/h\n",
                 fix.lat, fix.lon, utc, (unsigned)parser.satellites(),
                 hdop, parser.fix_quality(), fix.speed_kmh);
    } else {
        log_line("GNSS STATUS %s sats=%u HDOP=%s quality=%d (no usable coordinates)\n",
                 label(state), (unsigned)parser.satellites(),
                 hdop < 99.0f ? String(hdop, 2).c_str() : "unknown",
                 parser.fix_quality());
    }
#if ICVSP_DEBUG
    // Extra telemetry only when DEBUG is enabled; normal FIX/status lines always appear.
    static uint32_t last_debug_ms = 0;
    if (now - last_debug_ms >= ICVSP_DEBUG_EVERY_S * 1000UL) {
        last_debug_ms = now;
        log_line("GNSS DEBUG bytes=%lu valid_NMEA=%lu bad_checksum=%lu overflow=%lu uptime=%lus\n",
                 (unsigned long)bytes_seen, (unsigned long)stats.sentences,
                 (unsigned long)stats.checksum_errors, (unsigned long)stats.overflows,
                 (unsigned long)(now / 1000));
    }
#endif
}
