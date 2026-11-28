#include "nmea.h"

#include <cstdlib>
#include <cstring>

namespace icvsp {

int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

namespace {

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// An unsigned decimal "123" or "123.45" and nothing else (no sign, exponent, spaces, "nan" or "inf").
bool parse_decimal(const char* s, double& out) {
    const char* p = s;
    if (!is_digit(*p)) return false;
    while (is_digit(*p)) ++p;
    if (*p == '.') {
        ++p;
        while (is_digit(*p)) ++p;
    }
    if (*p) return false;
    out = std::strtod(s, nullptr);
    return true;
}

// "ddmm.mmmm" (latitude, 2 degree digits) or "dddmm.mmmm" (longitude, 3) + a one-letter hemisphere that
// belongs to that axis. Returns false on anything malformed or impossible.
bool parse_coord(const char* v, const char* hemi, bool latitude, double& out) {
    const int deg_digits = latitude ? 2 : 3;
    const int max_deg = latitude ? 90 : 180;
    if (std::strlen(hemi) != 1) return false;
    const bool negative = latitude ? *hemi == 'S' : *hemi == 'W';
    const bool positive = latitude ? *hemi == 'N' : *hemi == 'E';
    if (!negative && !positive) return false;

    int deg = 0;
    for (int i = 0; i < deg_digits; ++i) {
        if (!is_digit(v[i])) return false;
        deg = deg * 10 + (v[i] - '0');
    }
    if (!is_digit(v[deg_digits]) || !is_digit(v[deg_digits + 1])) return false;  // whole minutes: two digits
    double minutes;
    if (!parse_decimal(v + deg_digits, minutes) || minutes >= 60.0) return false;
    const double r = deg + minutes / 60.0;
    if (r > max_deg) return false;
    out = negative ? -r : r;
    return true;
}

// An unsigned integer of at most 4 digits, no larger than max.
bool parse_uint(const char* s, int max, int& out) {
    int v = 0, n = 0;
    for (; is_digit(*s); ++s, ++n) {
        if (n == 4) return false;
        v = v * 10 + (*s - '0');
    }
    if (n == 0 || *s || v > max) return false;
    out = v;
    return true;
}

bool two_digits(const char* p, int& out) {
    if (!is_digit(p[0]) || !is_digit(p[1])) return false;
    out = (p[0] - '0') * 10 + (p[1] - '0');
    return true;
}

int days_in_month(int year, int month) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : kDays[month - 1];
}

// "hhmmss" or "hhmmss.f" .. "hhmmss.fff" -> milliseconds of the day.
bool parse_time(const char* t, int64_t& ms_of_day) {
    int hh, mm, ss;
    if (!two_digits(t, hh) || !two_digits(t + 2, mm) || !two_digits(t + 4, ss)) return false;
    if (hh > 23 || mm > 59 || ss > 59) return false;  // a leap second (:60) is skipped, the next sentence resumes
    int ms = 0;
    const char* p = t + 6;
    if (*p == '.') {
        ++p;
        int scale = 100, n = 0;
        while (is_digit(*p) && n < 3) {
            ms += (*p - '0') * scale;
            scale /= 10;
            ++p;
            ++n;
        }
        if (n == 0) return false;
        while (is_digit(*p)) ++p;  // more than millisecond precision: ignore the rest
    }
    if (*p) return false;
    ms_of_day = ((hh * 60LL + mm) * 60 + ss) * 1000 + ms;
    return true;
}

// "ddmmyy" -> days since 1970 (years 2000-2099).
bool parse_date(const char* d, int64_t& days) {
    int dd, mo, yy;
    if (std::strlen(d) != 6 || !two_digits(d, dd) || !two_digits(d + 2, mo) || !two_digits(d + 4, yy)) return false;
    if (mo < 1 || mo > 12 || dd < 1 || dd > days_in_month(2000 + yy, mo)) return false;
    days = days_from_civil(2000 + yy, static_cast<unsigned>(mo), static_cast<unsigned>(dd));
    return true;
}

}  // namespace

uint64_t NmeaParser::clock64(uint32_t now_ms) const {
    if (!clock_started_) {
        clock_started_ = true;
        last_ms_ = now_ms;
        ext_ms_ = now_ms;
        return ext_ms_;
    }
    const uint32_t delta = now_ms - last_ms_;  // wrap-safe
    if (delta > 0xFFFFFFFFu - kBackTolMs) {    // up to 65 s older than the last call: map it into the past
        const uint32_t back = 0u - delta;
        return ext_ms_ - back;
    }
    ext_ms_ += delta;  // forward, anything up to ~49.7 days
    last_ms_ = now_ms;
    return ext_ms_;
}

void NmeaParser::feed(char c, uint32_t now_ms) {
    if (c == '$') {  // a new sentence always restarts, even after garbage
        in_sentence_ = true;
        overflowed_ = false;
        len_ = 0;
        return;
    }
    if (!in_sentence_) return;
    if (c == '\r' || c == '\n') {
        in_sentence_ = false;
        if (overflowed_) return;
        buf_[len_] = '\0';
        handle(buf_, clock64(now_ms));
        return;
    }
    if (len_ >= kMaxSentence) {
        if (!overflowed_) ++stats_.overflows;
        overflowed_ = true;
        return;
    }
    buf_[len_++] = c;
}

void NmeaParser::handle(char* body, uint64_t now) {
    char* star = std::strrchr(body, '*');
    if (!star || std::strlen(star) != 3) {
        ++stats_.checksum_errors;
        return;
    }
    const int hi = hex_digit(star[1]), lo = hex_digit(star[2]);
    uint8_t sum = 0;
    for (const char* p = body; p < star; ++p) sum ^= static_cast<uint8_t>(*p);
    if (hi < 0 || lo < 0 || sum != (hi << 4 | lo)) {
        ++stats_.checksum_errors;
        return;
    }
    ++stats_.sentences;
    *star = '\0';

    char* fields[24];
    size_t n = 0;
    char* p = body;
    fields[n++] = p;
    while (*p && n < 24) {
        if (*p == ',') {
            *p = '\0';
            fields[n++] = p + 1;
        }
        ++p;
    }
    if (std::strlen(fields[0]) != 5) return;  // "GNRMC": two talker letters + three type letters
    const char* type = fields[0] + 2;
    if (std::strcmp(type, "RMC") == 0) handle_rmc(fields, n, now);
    else if (std::strcmp(type, "GGA") == 0) handle_gga(fields, n, now);
}

// $xxRMC,hhmmss.sss,A,ddmm.mmmm,N,dddmm.mmmm,E,speed_knots,course,ddmmyy,mag,magdir,mode*cs
void NmeaParser::handle_rmc(char** f, size_t n, uint64_t now) {
    if (n < 10) return;
    have_rmc_ = true;
    rmc_rx_ = now;
    double lat, lon;
    // Mode (NMEA 2.3 and later, field 12): A autonomous, D differential are measured. E estimated (dead
    // reckoning), M manual input, S simulator and N no fix are not a measured position; a receiver can send
    // status A together with mode E. Older receivers have no mode field: then the status alone decides.
    const bool measured_mode = n <= 12 || f[12][0] == '\0' || f[12][0] == 'A' || f[12][0] == 'D';
    active_ = f[2][0] == 'A' && f[2][1] == '\0' && measured_mode && parse_coord(f[3], f[4], true, lat) &&
              parse_coord(f[5], f[6], false, lon);
    if (active_) {
        lat_ = lat;
        lon_ = lon;
        ever_active_ = true;
    }
    double v;
    // missing, malformed or absurd (> 1000 knots): unknown
    speed_kmh_ = parse_decimal(f[7], v) && v <= 1000.0 ? static_cast<float>(v * 1.852) : 0.0f;
    if (parse_decimal(f[8], v) && v < 360.0) course_deg_ = static_cast<float>(v);  // else keep the last course

    // Time: trusted only once the module has had a real fix this boot (before that its clock may be a
    // leftover or a default date). Afterwards a time-only RMC (fix lost) still keeps the clock right.
    // Simulator (S) and manual (M) sentences carry a time somebody typed or replayed, never the receiver's own
    // clock: they never set the time. E and N still do (the receiver keeps its clock while it estimates or
    // has lost the fix).
    const bool own_clock = n <= 12 || (f[12][0] != 'S' && f[12][0] != 'M');
    int64_t ms_of_day, days;
    if (ever_active_ && own_clock && parse_time(f[1], ms_of_day) && parse_date(f[9], days)) {
        utc_base_ms_ = days * 86400000LL + ms_of_day;
        utc_base_rx_ = now;
        have_utc_ = true;
    }
}

// $xxGGA,hhmmss.sss,lat,N,lon,E,quality,sats,hdop,alt,M,geoid,M,,*cs
void NmeaParser::handle_gga(char** f, size_t n, uint64_t now) {
    if (n < 9) return;
    int q, s;
    double h;
    quality_ = parse_uint(f[6], 8, q) ? q : 0;  // NMEA fix quality 0..8; anything else: no fix
    sats_ = parse_uint(f[7], 99, s) ? static_cast<uint8_t>(s) : 0;
    hdop_ = parse_decimal(f[8], h) && h <= 99.0 ? static_cast<float>(h) : 99.0f;
    have_gga_ = true;
    gga_rx_ = now;
}

GnssFix NmeaParser::fix(uint32_t now_ms) const {
    const uint64_t now = clock64(now_ms);
    GnssFix r{};
    // signed ages: a fix asked for a moment slightly before the last sentence is not "stale"
    const int64_t rmc_age = static_cast<int64_t>(now) - static_cast<int64_t>(rmc_rx_);
    const int64_t gga_age = static_cast<int64_t>(now) - static_cast<int64_t>(gga_rx_);
    const bool rmc_fresh = have_rmc_ && rmc_age <= kStaleMs;
    const bool gga_fresh = have_gga_ && gga_age <= kStaleMs;
    // GGA quality 1 (GPS) and 2 (differential) are measured fixes. 6 is estimated (dead reckoning), 7 manual
    // input, 8 simulation: never a position for a report. 3-5 (PPS, RTK) are not sent by the L76K or NEO-6M.
    const bool measured_quality = quality_ == 1 || quality_ == 2;
    r.valid = rmc_fresh && gga_fresh && active_ && measured_quality;
    if (r.valid) {
        r.lat = lat_;
        r.lon = lon_;
        r.acc_m = hdop_ * kUereM;
        r.speed_kmh = speed_kmh_;
        r.heading_deg = course_deg_;  // last known course (empty while standing still)
    }
    if (have_utc_) r.utc_ms = utc_base_ms_ + (static_cast<int64_t>(now) - static_cast<int64_t>(utc_base_rx_));
    return r;
}

}  // namespace icvsp
