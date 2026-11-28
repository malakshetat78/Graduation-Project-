// T-014: Seeed L76K GNSS hardware bring-up, ESP-IDF version of Ahmed's Arduino test. No Wi-Fi, no cloud.
// The checked NMEA parser (components/icvsp_board/nmea.cpp) does the parsing; this file only reads the UART,
// decides the state and prints it.
#include "gnss_test.h"

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nmea.h"
#include "sdkconfig.h"

namespace {

void log_line(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    fflush(stdout);
}

uint32_t millis() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

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

const char* label(Status state)
{
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

// The receiver sends valid sentences indoors too, without a satellite fix.
// Only an up-to-date RMC+GGA pair with a valid status and quality is a valid fix.
Status get_status(uint32_t now_ms, const icvsp::GnssFix& fix)
{
    if (got_any_byte && now_ms - last_byte_ms > 3500) return Status::DISCONNECTED;
    if (!got_any_byte) return now_ms - start_ms < 5000 ? Status::WAITING_UART : Status::DISCONNECTED;
    if (!got_valid_nmea) return Status::NO_VALID_NMEA;
    if (now_ms - last_valid_nmea_ms > 3500) return Status::STALE;
    if (!parser.has_position_sentences()) return Status::WAITING_RMC_GGA;
    if (!parser.position_sentences_fresh(now_ms)) return Status::STALE;
    return fix.valid ? Status::FIX : Status::NO_FIX;
}

#if CONFIG_ICVSP_GNSS_SOFTWARE_TESTS
// The real UART is still read during a simulation, so old buffered sentences cannot fake a recovery.
enum class Test { NONE, STALE, DISCONNECT };
Test test = Test::NONE;
uint32_t test_started_ms = 0;
bool recovering = false;
bool saw_expected = false;
bool wait_for_sentence = false;

void test_commands(uint32_t now)
{
    char ch = 0;
    for (int n = 0; n < 32 && usb_serial_jtag_read_bytes(&ch, 1, 0) == 1; ++n) {
        if (ch == 's') ch = 'S';
        if (ch == 'd') ch = 'D';
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
        log_line("GNSS TEST timeout %s; waiting for fresh RMC+GGA\n", saw_expected ? "PASS" : "FAIL");
    }
}

void test_observe(uint32_t now, Status state)
{
    if (test == Test::NONE) return;
    const Status expected = test == Test::STALE ? Status::STALE : Status::DISCONNECTED;
    if (!recovering && state == expected) saw_expected = true;
    if (recovering && state == Status::FIX) {
        log_line("GNSS TEST %c %s: fresh fix recovered\n", test == Test::STALE ? 'S' : 'D',
                 saw_expected ? "PASS" : "FAIL");
        test = Test::NONE;
    } else if (recovering && now - test_started_ms >= 21000) {
        log_line("GNSS TEST FAIL: no valid recovery within 15000ms\n");
        test = Test::NONE;
    }
}

void start_key_input()
{
    usb_serial_jtag_driver_config_t config = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    if (usb_serial_jtag_driver_install(&config) == ESP_OK) {
        usb_serial_jtag_vfs_use_driver();  // printf goes through the same driver as the key input
    } else {
        log_line("GNSS WARN key input not available: S and D will not work\n");
    }
}
#endif

void format_utc(char* out, size_t out_sz, int64_t utc_ms)
{
    if (utc_ms <= 0) {
        snprintf(out, out_sz, "unknown (wait for valid fix)");
        return;
    }
    const time_t epoch_s = static_cast<time_t>(utc_ms / 1000);
    struct tm utc_tm = {};
    if (gmtime_r(&epoch_s, &utc_tm) == nullptr || strftime(out, out_sz, "%Y-%m-%dT%H:%M:%SZ", &utc_tm) == 0) {
        snprintf(out, out_sz, "unknown");
    }
}

bool start_uart(const GnssPins& pins)
{
    uart_config_t config{};
    config.baud_rate = pins.baud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    esp_err_t err = uart_driver_install(pins.port, 2048, 0, 0, nullptr, 0);
    if (err == ESP_OK) err = uart_param_config(pins.port, &config);
    if (err == ESP_OK) err = uart_set_pin(pins.port, pins.tx_gpio, pins.rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        log_line("GNSS FAIL UART setup: %s\n", esp_err_to_name(err));
        return false;
    }
    return true;
}

// ---- Sky check (diagnostic only, not used for the fix) ----------------------------------------------------
// GSV sentences list the satellites the module can see and their signal strength (SNR, dB-Hz), also before
// it can use them for a fix. 0 in view means the antenna hears nothing; satellites with SNR under about 20
// mean a weak signal (window, roof, antenna covered). TXT sentences are module messages (e.g. antenna state).
struct SkyTalker {
    char id[3] = {};
    int in_view = 0;
    int heard = 0;      // satellites with an SNR value in the current GSV group
    int best_snr = 0;
    int build_heard = 0;
    int build_best = 0;
};
SkyTalker sky[4];
char raw_line[100];
size_t raw_len = 0;
char last_txt[80] = {};

bool checksum_ok(const char* s)
{
    if (s[0] != '$') return false;
    uint8_t sum = 0;
    const char* p = s + 1;
    while (*p != '\0' && *p != '*') sum ^= static_cast<uint8_t>(*p++);
    if (*p != '*') return false;
    unsigned want = 0;
    return sscanf(p + 1, "%2x", &want) == 1 && want == sum;
}

void handle_raw_sentence(char* s)
{
    if (!checksum_ok(s) || strlen(s) < 7) return;
    *strchr(s, '*') = '\0';
    if (strncmp(s + 3, "TXT", 3) == 0) {
        const char* text = strrchr(s, ',');
        if (text != nullptr && strcmp(text + 1, last_txt) != 0) {
            snprintf(last_txt, sizeof last_txt, "%s", text + 1);
            log_line("GNSS MODULE TXT %s\n", last_txt);
        }
        return;
    }
    if (strncmp(s + 3, "GSV", 3) != 0) return;
    char* field[24];
    size_t n = 0;
    for (char* p = s; n < 24;) {
        field[n++] = p;
        p = strchr(p, ',');
        if (p == nullptr) break;
        *p++ = '\0';
    }
    if (n < 4) return;
    SkyTalker* t = nullptr;
    for (SkyTalker& c : sky) {
        if (c.id[0] == '\0' || (c.id[0] == s[1] && c.id[1] == s[2])) {
            t = &c;
            break;
        }
    }
    if (t == nullptr) return;
    t->id[0] = s[1];
    t->id[1] = s[2];
    const int msg_total = atoi(field[1]);
    const int msg_num = atoi(field[2]);
    if (msg_num == 1) {
        t->build_heard = 0;
        t->build_best = 0;
    }
    t->in_view = atoi(field[3]);
    for (size_t i = 7; i < n; i += 4) {
        if (field[i][0] == '\0') continue;
        const int snr = atoi(field[i]);
        ++t->build_heard;
        if (snr > t->build_best) t->build_best = snr;
    }
    if (msg_num == msg_total) {
        t->heard = t->build_heard;
        t->best_snr = t->build_best;
    }
}

void feed_raw(char ch)
{
    if (ch == '$') raw_len = 0;
    if (ch == '\r' || ch == '\n') {
        if (raw_len > 0) {
            raw_line[raw_len] = '\0';
            handle_raw_sentence(raw_line);
        }
        raw_len = 0;
        return;
    }
    if (raw_len < sizeof raw_line - 1) raw_line[raw_len++] = ch;
}

void print_sky()
{
    char text[160];
    int len = snprintf(text, sizeof text, "GNSS SKY");
    bool any = false;
    for (const SkyTalker& t : sky) {
        if (t.id[0] == '\0') continue;
        any = true;
        len += snprintf(text + len, sizeof text - len, " %s: in_view=%d with_signal=%d best_SNR=%d", t.id,
                        t.in_view, t.heard, t.best_snr);
        if (len >= static_cast<int>(sizeof text)) break;
    }
    log_line("%s%s\n", text, any ? "" : " no GSV sentences yet");
}

void read_uart(const GnssPins& pins)
{
    uint8_t buf[256];
    // Up to 2 reads per loop (512 bytes), so the receive buffer cannot overflow at 9600 baud.
    for (int round = 0; round < 2; ++round) {
        const int got = uart_read_bytes(pins.port, buf, sizeof buf, 0);
        if (got <= 0) return;
        for (int i = 0; i < got; ++i) {
            const char ch = static_cast<char>(buf[i]);
            const uint32_t now = millis();
            feed_raw(ch);  // sky check sees the real data, also during the S/D simulations
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
            parser.feed(ch, now);
            const uint32_t good = parser.stats().sentences;
            if (good != last_good_count) {
                last_good_count = good;
                last_valid_nmea_ms = now;
                got_valid_nmea = true;
            }
        }
    }
}

void print_line(uint32_t now, Status state, const icvsp::GnssFix& fix)
{
    const float hdop = parser.hdop();
    char hdop_text[16];
    if (hdop < 99.0f) {
        snprintf(hdop_text, sizeof hdop_text, "%.2f", static_cast<double>(hdop));
    } else {
        snprintf(hdop_text, sizeof hdop_text, "unknown");
    }
    if (state == Status::FIX) {
        char utc[40];
        format_utc(utc, sizeof utc, fix.utc_ms);
        char where[64];
#if CONFIG_ICVSP_GNSS_PRINT_COORDS
        const int dec = CONFIG_ICVSP_GNSS_COORD_DECIMALS;
        snprintf(where, sizeof where, "lat=%.*f lon=%.*f", dec, fix.lat, dec, fix.lon);
#else
        snprintf(where, sizeof where, "lat=hidden lon=hidden");  // private: see ICVSP_GNSS_PRINT_COORDS
#endif
        log_line("GNSS FIX %s UTC=%s sats=%u HDOP=%s quality=%d speed=%.1fkm/h\n", where, utc,
                 static_cast<unsigned>(parser.satellites()), hdop_text, parser.fix_quality(),
                 static_cast<double>(fix.speed_kmh));
    } else {
        log_line("GNSS STATUS %s sats=%u HDOP=%s quality=%d (no usable coordinates)\n", label(state),
                 static_cast<unsigned>(parser.satellites()), hdop_text, parser.fix_quality());
    }
#if CONFIG_ICVSP_GNSS_DEBUG_EVERY_S > 0
    static uint32_t last_debug_ms = 0;
    if (now - last_debug_ms >= CONFIG_ICVSP_GNSS_DEBUG_EVERY_S * 1000UL) {
        last_debug_ms = now;
        const icvsp::NmeaParser::Stats& stats = parser.stats();
        log_line("GNSS DEBUG bytes=%lu valid_NMEA=%lu bad_checksum=%lu overflow=%lu uptime=%lus\n",
                 static_cast<unsigned long>(bytes_seen), static_cast<unsigned long>(stats.sentences),
                 static_cast<unsigned long>(stats.checksum_errors), static_cast<unsigned long>(stats.overflows),
                 static_cast<unsigned long>(now / 1000));
        print_sky();
    }
#else
    (void)now;
#endif
}

}  // namespace

void run_gnss_test(const GnssPins& pins)
{
#if CONFIG_ICVSP_GNSS_SOFTWARE_TESTS
    start_key_input();
#endif
    start_ms = millis();
    log_line("\nGNSS BEGIN T-014: Seeed L76K bring-up (ESP-IDF build)\n");
    log_line("GNSS UART%d baud=%d RX=GPIO%d(D7) TX=GPIO%d(D6)\n", static_cast<int>(pins.port), pins.baud,
             pins.rx_gpio, pins.tx_gpio);
    log_line("GNSS INFO module TX -> ECU RX, module RX <- ECU TX; NO WIFI / NO CLOUD\n");
    log_line("GNSS INFO indoor NO_FIX is expected; test outdoors or at a window with clear sky\n");
#if CONFIG_ICVSP_GNSS_PRINT_COORDS
    log_line("GNSS INFO coordinates printed with %d decimals: private log, do not share\n",
             CONFIG_ICVSP_GNSS_COORD_DECIMALS);
#else
    log_line("GNSS INFO coordinates hidden (lat=hidden lon=hidden)\n");
#endif
#if CONFIG_ICVSP_GNSS_SOFTWARE_TESTS
    log_line("GNSS TEST enabled: S=stale, D=disconnect; start from VALID_FIX\n");
#endif
    if (!start_uart(pins)) {
        for (;;) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    for (;;) {
        uint32_t now = millis();
#if CONFIG_ICVSP_GNSS_SOFTWARE_TESTS
        test_commands(now);
#endif
        read_uart(pins);
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
        if (now - last_print_ms >= CONFIG_ICVSP_GNSS_PRINT_EVERY_MS) {
            last_print_ms = now;
            print_line(now, state, fix);
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
