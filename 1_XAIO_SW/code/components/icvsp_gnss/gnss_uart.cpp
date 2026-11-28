#include "gnss_uart.h"

#include <cstdio>

bool GnssUart::begin(const Pins& pins)
{
    port_ = pins.port;
    uart_config_t config{};  // same settings as the T-014 test: 9600 8N1, no flow control
    config.baud_rate = pins.baud;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;
    esp_err_t err = uart_driver_install(port_, 2048, 0, 0, nullptr, 0);
    if (err == ESP_OK) err = uart_param_config(port_, &config);
    if (err == ESP_OK) err = uart_set_pin(port_, pins.tx_gpio, pins.rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    ok_ = err == ESP_OK;
    std::printf(ok_ ? "GNSS UART%d ready (TX GPIO%d, RX GPIO%d, %d baud)\n" : "GNSS FAIL UART%d setup (TX GPIO%d, RX GPIO%d, %d baud)\n",
                static_cast<int>(port_), pins.tx_gpio, pins.rx_gpio, pins.baud);
    return ok_;
}

int GnssUart::poll(icvsp::NmeaParser& parser, uint32_t now_ms, int max_bytes)
{
    if (!ok_) return 0;
    uint8_t buf[128];
    int total = 0;
    while (total < max_bytes) {
        const int want = max_bytes - total < static_cast<int>(sizeof buf) ? max_bytes - total : static_cast<int>(sizeof buf);
        const int got = uart_read_bytes(port_, buf, want, 0);  // 0: never wait
        if (got <= 0) break;
        for (int i = 0; i < got; i++) parser.feed(static_cast<char>(buf[i]), now_ms);
        total += got;
    }
    if (total > 0) {
        bytes_ += total;
        last_byte_ms_ = now_ms;
    }
    return total;
}
