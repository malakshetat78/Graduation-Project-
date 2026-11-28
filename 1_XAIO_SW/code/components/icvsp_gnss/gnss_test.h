#pragma once
// T-014 L76K GNSS bring-up test (original Arduino version by Ahmed). Reads NMEA from the module over UART,
// prints one line per second and one line per state change:
//   GNSS STATUS <state> sats=.. HDOP=.. quality=.. (no usable coordinates)
//   GNSS FIX lat=hidden lon=hidden UTC=.. sats=.. HDOP=.. quality=.. speed=..km/h
//   GNSS EVENT <old state> -> <new state>
// States: WAITING_UART, NO_VALID_NMEA, WAITING_RMC_GGA, DISCONNECTED, STALE, NO_FIX, VALID_FIX.
// With CONFIG_ICVSP_GNSS_SOFTWARE_TESTS, typing S or D in the monitor (during VALID_FIX) hides the UART data
// for 6 s as stale (S) or as a lost connection (D), then checks that a fresh fix comes back.
#include "driver/uart.h"

struct GnssPins {
    uart_port_t port;
    int tx_gpio;  // ESP32 TX -> module RX
    int rx_gpio;  // ESP32 RX <- module TX
    int baud;
};

[[noreturn]] void run_gnss_test(const GnssPins& pins);
