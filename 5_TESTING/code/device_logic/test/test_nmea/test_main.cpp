// GNSS NMEA parser (board layer; NEO-6M on the parts list, L76K uses the same sentences). Sentences built with a correct checksum unless a test breaks it.
#include <unity.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "nmea.h"

using namespace icvsp;

namespace {

std::string sentence(const char* body) {
    unsigned sum = 0;
    for (const char* p = body; *p; ++p) sum ^= static_cast<unsigned char>(*p);
    char tail[8];
    std::snprintf(tail, sizeof tail, "*%02X\r\n", sum);
    return std::string("$") + body + tail;
}

// Cairo, Nasr City, moving east at 36 km/h (19.44 knots), 15 Nov 2026 10:20:30.500 UTC
const char* kRmc = "GNRMC,102030.500,A,3002.6640,N,03116.9620,E,19.44,90.0,151126,,,A";
const char* kGga = "GNGGA,102030.500,3002.6640,N,03116.9620,E,1,09,0.8,25.0,M,15.0,M,,";
const int64_t kUtc = 1794738030500;  // 2026-11-15T10:20:30.500Z

}  // namespace

void setUp() {}
void tearDown() {}

void test_days_from_civil_matches_known_dates() {
    TEST_ASSERT_EQUAL_INT64(0, days_from_civil(1970, 1, 1));
    TEST_ASSERT_EQUAL_INT64(10957, days_from_civil(2000, 1, 1));
    TEST_ASSERT_EQUAL_INT64(20772, days_from_civil(2026, 11, 15));
    TEST_ASSERT_EQUAL_INT64(19782, days_from_civil(2024, 2, 29));  // leap day
}

void test_textbook_rmc_and_gga_give_the_position() {
    NmeaParser p;
    p.feed("$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n", 1000);
    p.feed("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n", 1000);
    const GnssFix f = p.fix(1000);
    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 48.1173, f.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 11.516666, f.lon);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 22.4f * 1.852f, f.speed_kmh);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 84.4f, f.heading_deg);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.9f * NmeaParser::kUereM, f.acc_m);
    TEST_ASSERT_EQUAL_UINT8(8, p.satellites());
    TEST_ASSERT_EQUAL_UINT32(2, p.stats().sentences);
}

void test_multi_constellation_sentences_with_utc_time() {
    NmeaParser p;
    p.feed(sentence(kRmc).c_str(), 5000);
    p.feed(sentence(kGga).c_str(), 5000);
    const GnssFix f = p.fix(5000);
    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 30.0444, f.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, 31.2827, f.lon);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 36.0f, f.speed_kmh);
    TEST_ASSERT_EQUAL_INT64(kUtc, f.utc_ms);
}

void test_time_moves_forward_with_the_device_clock_between_sentences() {
    NmeaParser p;
    p.feed(sentence(kGga).c_str(), 5000);
    p.feed(sentence(kRmc).c_str(), 5000);
    TEST_ASSERT_EQUAL_INT64(kUtc + 730, p.fix(5730).utc_ms);
}

void test_bad_checksum_is_ignored_and_counted() {
    NmeaParser p;
    std::string s = sentence(kRmc);
    s[10] = '3';  // corrupt one character of the time
    p.feed(s.c_str(), 100);
    p.feed("$GNRMC,102030.500,A,3002.6640,N,03116.9620,E,19.44,90.0,151126,,,A\r\n", 100);  // no checksum
    TEST_ASSERT_FALSE(p.fix(100).valid);
    TEST_ASSERT_EQUAL_UINT32(2, p.stats().checksum_errors);
    TEST_ASSERT_EQUAL_UINT32(0, p.stats().sentences);
}

void test_no_fix_yet_means_no_position_and_no_time() {
    NmeaParser p;
    // what the module prints after power-up: an RTC date, status V, no position
    p.feed(sentence("GNRMC,000012.000,V,,,,,0.00,0.00,060180,,,N").c_str(), 100);
    p.feed(sentence("GNGGA,000012.000,,,,,0,00,25.5,,,,,,").c_str(), 100);
    const GnssFix f = p.fix(100);
    TEST_ASSERT_FALSE(f.valid);
    TEST_ASSERT_EQUAL_INT64(0, f.utc_ms);  // the 1980 default date must never reach a report
    TEST_ASSERT_FALSE(p.has_time());
}

void test_time_survives_a_lost_fix_but_position_does_not() {
    NmeaParser p;
    p.feed(sentence(kGga).c_str(), 1000);
    p.feed(sentence(kRmc).c_str(), 1000);
    p.feed(sentence("GNRMC,102031.500,V,,,,,,,151126,,,N").c_str(), 2000);  // tunnel
    p.feed(sentence("GNGGA,102031.500,,,,,0,00,99.9,,,,,,").c_str(), 2000);
    const GnssFix f = p.fix(2000);
    TEST_ASSERT_FALSE(f.valid);
    TEST_ASSERT_EQUAL_INT64(kUtc + 1000, f.utc_ms);
}

void test_silence_makes_the_fix_stale() {
    NmeaParser p;
    p.feed(sentence(kGga).c_str(), 1000);
    p.feed(sentence(kRmc).c_str(), 1000);
    TEST_ASSERT_TRUE(p.fix(1000 + NmeaParser::kStaleMs).valid);
    TEST_ASSERT_FALSE(p.fix(1001 + NmeaParser::kStaleMs).valid);
    TEST_ASSERT_TRUE(p.fix(1001 + NmeaParser::kStaleMs).utc_ms > 0);
}

void test_garbage_and_split_bytes_are_handled() {
    NmeaParser p;
    const char junk[] = {'\0', '\xff', 'n', 'o', 'i', 's', 'e', '\r', '\n', '$', 'G', 'N', 'G'};  // NUL, noise, cut-off
    for (char c : junk) p.feed(c, 10);
    TEST_ASSERT_EQUAL_UINT32(0, p.stats().sentences);
    std::string s = sentence(kRmc);
    for (char c : s) p.feed(c, 20);              // byte by byte, as from the UART
    p.feed(sentence(kGga).c_str(), 20);
    TEST_ASSERT_TRUE(p.fix(20).valid);
    std::string longline = "$GNRMC," + std::string(200, '1') + "*00\r\n";
    p.feed(longline.c_str(), 30);
    TEST_ASSERT_EQUAL_UINT32(1, p.stats().overflows);
    TEST_ASSERT_TRUE(p.fix(30).valid);           // the old fix is untouched
}

void test_southern_and_western_hemispheres_are_negative() {
    NmeaParser p;
    p.feed(sentence("GPRMC,120000.000,A,3352.1280,S,15112.5600,W,0.0,,151126,,,A").c_str(), 0);
    p.feed(sentence("GPGGA,120000.000,3352.1280,S,15112.5600,W,1,07,1.2,10,M,0,M,,").c_str(), 0);
    const GnssFix f = p.fix(0);
    TEST_ASSERT_TRUE(f.valid);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -33.8688, f.lat);
    TEST_ASSERT_DOUBLE_WITHIN(1e-6, -151.2093333, f.lon);
}

void test_malformed_coordinates_do_not_give_a_fix() {
    NmeaParser p;
    p.feed(sentence("GNRMC,102030.500,A,30X2.6640,N,03116.9620,E,0,0,151126,,,A").c_str(), 0);
    p.feed(sentence(kGga).c_str(), 0);
    TEST_ASSERT_FALSE(p.fix(0).valid);
}

void test_impossible_coordinates_are_rejected() {
    const char* bad[] = {
        "GNRMC,102030.500,A,9100.0000,N,03116.9620,E,19.44,90.0,151126,,,A",   // latitude 91
        "GNRMC,102030.500,A,9000.0100,N,03116.9620,E,19.44,90.0,151126,,,A",   // just past the pole
        "GNRMC,102030.500,A,3002.6640,E,03116.9620,E,19.44,90.0,151126,,,A",   // latitude marked E
        "GNRMC,102030.500,A,3002.6640,N,03116.9620,N,19.44,90.0,151126,,,A",   // longitude marked N
        "GNRMC,102030.500,A,30nan,N,03116.9620,E,19.44,90.0,151126,,,A",       // NaN
        "GNRMC,102030.500,A,3002.6640,N,18100.0000,E,19.44,90.0,151126,,,A",   // longitude 181
        "GNRMC,102030.500,A,3002.6640,NN,03116.9620,E,19.44,90.0,151126,,,A",  // two-letter hemisphere
        "GNRMC,102030.500,A,30 2.6640,N,03116.9620,E,19.44,90.0,151126,,,A",   // space inside the minutes
    };
    for (const char* b : bad) {
        NmeaParser p;
        p.feed(sentence(kGga).c_str(), 0);
        p.feed(sentence(b).c_str(), 0);
        TEST_ASSERT_FALSE_MESSAGE(p.fix(0).valid, b);
    }
}

void test_missing_speed_is_unknown_not_the_old_speed() {
    NmeaParser p;
    p.feed(sentence(kGga).c_str(), 1000);
    p.feed(sentence(kRmc).c_str(), 1000);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 36.0f, p.fix(1000).speed_kmh);
    p.feed(sentence("GNGGA,102031.500,3002.6640,N,03116.9620,E,1,09,0.8,25.0,M,15.0,M,,").c_str(), 2000);
    p.feed(sentence("GNRMC,102031.500,A,3002.6640,N,03116.9620,E,,,151126,,,A").c_str(), 2000);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.fix(2000).speed_kmh);  // below the detector's 8 km/h gate: no false bumps
    p.feed(sentence("GNRMC,102032.500,A,3002.6640,N,03116.9620,E,1x.5,90.0,151126,,,A").c_str(), 3000);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.fix(3000).speed_kmh);  // malformed speed is unknown too
}

void test_fix_quality_expires_when_gga_stops() {
    NmeaParser p;
    p.feed(sentence(kGga).c_str(), 1000);
    p.feed(sentence(kRmc).c_str(), 1000);
    const std::string rmc = sentence("GNRMC,102031.500,A,3002.6640,N,03116.9620,E,19.44,90.0,151126,,,A");
    p.feed(rmc.c_str(), 2000);  // RMC keeps coming, GGA does not
    TEST_ASSERT_TRUE(p.fix(2000).valid);   // quality is 1 s old: fine
    p.feed(rmc.c_str(), 3000);
    p.feed(rmc.c_str(), 4000);
    TEST_ASSERT_FALSE(p.fix(4000).valid);  // quality / HDOP is 3 s old: not trusted any more
}

void test_impossible_dates_and_malformed_times_are_not_trusted() {
    const char* bad[] = {
        "GNRMC,102030.500,A,3002.6640,N,03116.9620,E,19.44,90.0,310226,,,A",  // 31 February
        "GNRMC,102030.500,A,3002.6640,N,03116.9620,E,19.44,90.0,290226,,,A",  // 29 Feb in a non-leap year
        "GNRMC,102030junk,A,3002.6640,N,03116.9620,E,19.44,90.0,151126,,,A",  // junk after the seconds
        "GNRMC,102030.5x0,A,3002.6640,N,03116.9620,E,19.44,90.0,151126,,,A",  // junk in the fraction
        "GNRMC,102060.000,A,3002.6640,N,03116.9620,E,19.44,90.0,151126,,,A",  // second 60
    };
    for (const char* b : bad) {
        NmeaParser p;
        p.feed(sentence(kGga).c_str(), 0);
        p.feed(sentence(b).c_str(), 0);
        TEST_ASSERT_FALSE_MESSAGE(p.has_time(), b);
    }
    NmeaParser leap;
    leap.feed(sentence(kGga).c_str(), 0);
    leap.feed(sentence("GNRMC,102030.500,A,3002.6640,N,03116.9620,E,19.44,90.0,290228,,,A").c_str(), 0);
    TEST_ASSERT_TRUE(leap.has_time());  // 29 Feb 2028 is real
}

void test_clock_stays_right_past_a_full_millis_period() {
    NmeaParser p;
    const uint32_t start = 1000;
    p.feed(sentence(kGga).c_str(), start);
    p.feed(sentence(kRmc).c_str(), start);
    // 50 days with no GNSS at all (antenna cut), the app asking for the fix every ~11.6 days;
    // millis() wraps once (after 49.7 days) on the way.
    const uint64_t step = 1000000000ULL, total = 50ULL * 24 * 3600 * 1000;
    uint64_t elapsed = 0;
    GnssFix f{};
    while (elapsed < total) {
        elapsed = elapsed + step > total ? total : elapsed + step;
        f = p.fix(static_cast<uint32_t>(start + elapsed));
    }
    TEST_ASSERT_FALSE(f.valid);  // must not look "fresh" again after the wrap
    TEST_ASSERT_EQUAL_INT64(kUtc + static_cast<int64_t>(total), f.utc_ms);
}

void test_absurd_numeric_fields_do_not_give_a_fix() {
    const char* bad_gga[] = {
        "GNGGA,102030.500,3002.6640,N,03116.9620,E,2147483648,09,0.8,25.0,M,15.0,M,,",  // overflows an int
        "GNGGA,102030.500,3002.6640,N,03116.9620,E,1.5,09,0.8,25.0,M,15.0,M,,",         // fractional quality
        "GNGGA,102030.500,3002.6640,N,03116.9620,E,9,09,0.8,25.0,M,15.0,M,,",           // no such quality
    };
    for (const char* b : bad_gga) {
        NmeaParser p;
        p.feed(sentence(kRmc).c_str(), 0);
        p.feed(sentence(b).c_str(), 0);
        TEST_ASSERT_FALSE_MESSAGE(p.fix(0).valid, b);
    }
    NmeaParser p;
    p.feed(sentence(kGga).c_str(), 0);
    p.feed(sentence("GNRMC,102030.500,A,3002.6640,N,03116.9620,E,99999.0,90.0,151126,,,A").c_str(), 0);
    TEST_ASSERT_TRUE(p.fix(0).valid);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, p.fix(0).speed_kmh);  // absurd speed (99999 knots) is unknown
}

void test_clock_survives_a_30_day_gap_between_calls() {
    NmeaParser p;
    p.feed(sentence(kGga).c_str(), 1000);
    p.feed(sentence(kRmc).c_str(), 1000);
    const uint64_t gap = 30ULL * 24 * 3600 * 1000;  // longer than 2^31 ms, shorter than 2^32
    const GnssFix f = p.fix(static_cast<uint32_t>(1000 + gap));
    TEST_ASSERT_EQUAL_INT64(kUtc + static_cast<int64_t>(gap), f.utc_ms);
}

void test_a_slightly_older_timestamp_is_placed_in_the_past() {
    NmeaParser p;
    p.fix(2000);  // the app asked for the fix at 2000 ...
    p.feed(sentence(kGga).c_str(), 1500);  // ... then bytes stamped 1500 are parsed
    p.feed(sentence(kRmc).c_str(), 1500);
    TEST_ASSERT_EQUAL_INT64(kUtc + 1000, p.fix(2500).utc_ms);  // anchored at 1500, not at 2000
    TEST_ASSERT_TRUE(p.fix(2500).valid);
}

void test_estimated_or_simulated_gga_quality_is_not_a_fix() {
    // GGA quality 6 = estimated (dead reckoning), 7 = manual input, 8 = simulation. Seen on the board: the
    // L76K reported quality 6 for 100 s outside (5_TESTING/doc/gnss/run3_outside_fix_S_D).
    const char* gga[] = {
        "GNGGA,102030.500,3002.6640,N,03116.9620,E,6,07,14.8,25.0,M,15.0,M,,",
        "GNGGA,102030.500,3002.6640,N,03116.9620,E,7,07,0.8,25.0,M,15.0,M,,",
        "GNGGA,102030.500,3002.6640,N,03116.9620,E,8,07,0.8,25.0,M,15.0,M,,",
    };
    for (const char* g : gga) {
        NmeaParser p;
        p.feed(sentence(kRmc).c_str(), 1000);
        p.feed(sentence(g).c_str(), 1000);
        TEST_ASSERT_FALSE(p.fix(1000).valid);
    }
}

void test_estimated_or_simulated_rmc_mode_is_not_a_fix() {
    // Status A but mode E (estimated), M (manual), S (simulator) or N (no fix): no position, no trusted time.
    const char* rmc[] = {
        "GNRMC,102030.500,A,3002.6640,N,03116.9620,E,19.44,90.0,151126,,,E",
        "GNRMC,102030.500,A,3002.6640,N,03116.9620,E,19.44,90.0,151126,,,M",
        "GNRMC,102030.500,A,3002.6640,N,03116.9620,E,19.44,90.0,151126,,,S",
        "GNRMC,102030.500,A,3002.6640,N,03116.9620,E,19.44,90.0,151126,,,N",
        "GNRMC,102030.500,A,3002.6640,N,03116.9620,E,19.44,90.0,151126,,,E,V",  // NMEA 4.1 with nav status
    };
    for (const char* r : rmc) {
        NmeaParser p;
        p.feed(sentence(kGga).c_str(), 1000);
        p.feed(sentence(r).c_str(), 1000);
        TEST_ASSERT_FALSE(p.fix(1000).valid);
        TEST_ASSERT_FALSE(p.has_time());
    }
}

void test_simulator_or_manual_rmc_never_changes_the_time() {
    // After a real fix, a simulator (S) or manual (M) RMC with another date must not move the clock.
    for (const char* r : {"GNRMC,000000.000,A,3002.6640,N,03116.9620,E,0.0,0.0,010180,,,S",
                          "GNRMC,000000.000,A,3002.6640,N,03116.9620,E,0.0,0.0,010180,,,M"}) {
        NmeaParser p;
        p.feed(sentence(kGga).c_str(), 1000);
        p.feed(sentence(kRmc).c_str(), 1000);
        p.feed(sentence(r).c_str(), 2000);
        TEST_ASSERT_EQUAL_INT64(kUtc + 1000, p.fix(2000).utc_ms);
    }
    // A lost-fix RMC (status V, mode N) from the receiver itself still keeps the clock right.
    NmeaParser q;
    q.feed(sentence(kGga).c_str(), 1000);
    q.feed(sentence(kRmc).c_str(), 1000);
    q.feed(sentence("GNRMC,102032.500,V,,,,,,,151126,,,N").c_str(), 2000);
    TEST_ASSERT_EQUAL_INT64(kUtc + 2000, q.fix(2000).utc_ms);
}

void test_differential_fix_is_valid() {
    NmeaParser p;
    p.feed(sentence("GNRMC,102030.500,A,3002.6640,N,03116.9620,E,19.44,90.0,151126,,,D").c_str(), 1000);
    p.feed(sentence("GNGGA,102030.500,3002.6640,N,03116.9620,E,2,09,0.8,25.0,M,15.0,M,,").c_str(), 1000);
    TEST_ASSERT_TRUE(p.fix(1000).valid);
    NmeaParser q;  // NMEA 4.1: mode A followed by nav status S (safe)
    q.feed(sentence("GNRMC,102030.500,A,3002.6640,N,03116.9620,E,19.44,90.0,151126,,,A,S").c_str(), 1000);
    q.feed(sentence(kGga).c_str(), 1000);
    TEST_ASSERT_TRUE(q.fix(1000).valid);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_days_from_civil_matches_known_dates);
    RUN_TEST(test_textbook_rmc_and_gga_give_the_position);
    RUN_TEST(test_multi_constellation_sentences_with_utc_time);
    RUN_TEST(test_time_moves_forward_with_the_device_clock_between_sentences);
    RUN_TEST(test_bad_checksum_is_ignored_and_counted);
    RUN_TEST(test_no_fix_yet_means_no_position_and_no_time);
    RUN_TEST(test_time_survives_a_lost_fix_but_position_does_not);
    RUN_TEST(test_silence_makes_the_fix_stale);
    RUN_TEST(test_garbage_and_split_bytes_are_handled);
    RUN_TEST(test_southern_and_western_hemispheres_are_negative);
    RUN_TEST(test_malformed_coordinates_do_not_give_a_fix);
    RUN_TEST(test_impossible_coordinates_are_rejected);
    RUN_TEST(test_missing_speed_is_unknown_not_the_old_speed);
    RUN_TEST(test_fix_quality_expires_when_gga_stops);
    RUN_TEST(test_impossible_dates_and_malformed_times_are_not_trusted);
    RUN_TEST(test_clock_stays_right_past_a_full_millis_period);
    RUN_TEST(test_absurd_numeric_fields_do_not_give_a_fix);
    RUN_TEST(test_clock_survives_a_30_day_gap_between_calls);
    RUN_TEST(test_a_slightly_older_timestamp_is_placed_in_the_past);
    RUN_TEST(test_estimated_or_simulated_gga_quality_is_not_a_fix);
    RUN_TEST(test_estimated_or_simulated_rmc_mode_is_not_a_fix);
    RUN_TEST(test_simulator_or_manual_rmc_never_changes_the_time);
    RUN_TEST(test_differential_fix_is_valid);
    return UNITY_END();
}
