#pragma once
// The L76K's UART for the all-in-one ECU firmware (main/ecu_main.cpp): installs the UART and hands every waiting
// byte to the shared NMEA parser, without blocking. The T-014 test (gnss_test.cpp) is separate and unchanged.
#include <cstdint>

#include "driver/uart.h"
#include "nmea.h"

class GnssUart {
public:
    struct Pins {
        uart_port_t port;
        int tx_gpio;  // ESP32 TX -> module RX
        int rx_gpio;  // ESP32 RX <- module TX
        int baud;
    };

    bool begin(const Pins& pins);
    // Reads at most max_bytes waiting bytes into the parser and returns how many (bounded, so the caller's loop
    // keeps its timing; 9600 baud is ~960 bytes/s, so 256 per 5 ms call never falls behind).
    int poll(icvsp::NmeaParser& parser, uint32_t now_ms, int max_bytes = 256);
    uint32_t bytes() const { return bytes_; }
    uint32_t last_byte_ms() const { return last_byte_ms_; }

private:
    uart_port_t port_ = UART_NUM_1;
    bool ok_ = false;
    uint32_t bytes_ = 0;
    uint32_t last_byte_ms_ = 0;
};
