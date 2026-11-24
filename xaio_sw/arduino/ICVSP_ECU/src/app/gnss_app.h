#pragma once
// Real Seeed L76K GNSS bring-up for ESP32-S3 XIAO: UART -> NMEA -> position/time/quality.
// These functions only run when config.h selects ICVSP_APP_GNSS.
void start_gnss_app();
void gnss_app_loop();
