#pragma once
// POSTs one signed report line to the backend (CONFIG_ICVSP_REPORT_URL, Cloud Run's POST /reports in the
// final setup). Implements icvsp::ReportTransport: it returns the status and the reply body, and the tested
// Uplink decides what to do with them (remove, keep for registration, refuse for good, or retry).
//
// https:// URLs check the server certificate against ESP-IDF's bundle of public root certificates; this
// needs the clock set first (SNTP or GNSS time). http:// is for a backend on the laptop only.
#include "uplink.h"

class EspHttpTransport : public icvsp::ReportTransport {
public:
    int post_report(const char* line, size_t len, char* reply, size_t reply_cap) override;
};
