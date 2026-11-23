#pragma once
// POSTs one signed report line to the backend (CONFIG_ICVSP_REPORT_URL in config.h). Implements
// icvsp::ReportTransport: returns the status and the reply, and the tested Uplink decides what to do
// (remove, keep for registration, refuse for good, or retry).
// https:// checks the server certificate against the public root certificate bundle built into the core.
// Arduino core limits: TLS 1.2 (TLS 1.3 is off in the core) and certificate dates are not checked.
#include "../board/uplink.h"

class EspHttpTransport : public icvsp::ReportTransport {
public:
    int post_report(const char* line, size_t len, char* reply, size_t reply_cap) override;
};
