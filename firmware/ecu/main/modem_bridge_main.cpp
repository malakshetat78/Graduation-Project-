// Modem AT bridge: the XIAO stands in for a USB-to-serial adapter, so the laptop can talk to a mobile modem
// (SIM900A first) before any modem code is written. Stages 1-2 of results/bringup/SIM900A_BRINGUP.md.
//
//   laptop (idf.py monitor) --USB--> XIAO --UART2 (D3 TX, D1 RX)--> modem
//   modem answers           <--USB-- XIAO <--UART2--
//
// What is typed is collected into a line and sent when Enter is pressed (idf.py monitor sends each key at once;
// AT commands are lines), with a carriage return as the modem expects. Each sent line is printed as "> ..." so the
// saved log shows what was asked; the modem's own echo can be turned off with ATE0. Modem output is passed through
// byte for byte. Lines starting with "~" are for the bridge itself:
//   ~help         list these commands
//   ~baud N       change the UART2 baud rate (the modem's rate; SIM900 detects it from the next "AT")
//   ~stats        bytes sent and received so far
//
// Hardware rules (see board_pins.h): the modem has its own 5 V / 2 A supply with a common ground, never the
// XIAO's pins; the SIM900's UART is 2.8 V, so check the breakout's level shifting first. There is no power-key pin
// (D3 is the modem TX now): use the breakout's own power button. D0/D2 belong to the stacked GPS, never used here. Nothing here touches NVS, Wi-Fi or the device key.
#include <cerrno>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board_pins.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_app_desc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr size_t kLineMax = 256;
constexpr int kUartBuf = 2048;

uint32_t g_sent = 0, g_received = 0;
int g_baud = CONFIG_ICVSP_MODEM_BAUD;

void usb_write(const char* s, size_t n) { usb_serial_jtag_write_bytes(s, n, pdMS_TO_TICKS(100)); }

void say(const char* s) { usb_write(s, std::strlen(s)); }

void help() {
    say("BRIDGE commands: ~help, ~baud N, ~stats. Anything else is sent to the modem when you press Enter.\n"
        "BRIDGE try: AT, ATE0, AT+CMEE=2, ATI, AT+CPIN?, AT+CSQ, AT+CREG?, AT+COPS?, AT+CGATT?, AT+CGDCONT?\n"
        "BRIDGE quit the monitor with Ctrl-]\n");
}

void local_command(const char* line) {
    char msg[128];
    if (std::strncmp(line, "~baud", 5) == 0) {
        errno = 0;
        char* end = nullptr;
        const long b = std::strtol(line + 5, &end, 10);
        if (errno != 0 || end == line + 5 || b < 1200 || b > 921600) {
            say("BRIDGE usage: ~baud N (1200..921600)\n");
            return;
        }
        if (uart_set_baudrate(MODEM_UART, static_cast<uint32_t>(b)) == ESP_OK) {
            g_baud = static_cast<int>(b);
            std::snprintf(msg, sizeof msg, "BRIDGE UART2 now %d baud (send AT a few times so the modem follows)\n",
                          g_baud);
        } else {
            std::snprintf(msg, sizeof msg, "BRIDGE could not set %ld baud\n", b);
        }
        say(msg);
    } else if (std::strcmp(line, "~stats") == 0) {
        std::snprintf(msg, sizeof msg, "BRIDGE baud=%d sent=%" PRIu32 " bytes received=%" PRIu32 " bytes\n", g_baud,
                      g_sent, g_received);
        say(msg);
    } else {
        help();
    }
}

void send_line(const char* line, size_t n) {
    say("> ");
    usb_write(line, n);
    say("\n");
    if (line[0] == '~') {
        local_command(line);
        return;
    }
    uart_write_bytes(MODEM_UART, line, n);
    uart_write_bytes(MODEM_UART, "\r", 1);
    g_sent += static_cast<uint32_t>(n + 1);
}

}  // namespace

extern "C" void app_main() {
    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_cfg.rx_buffer_size = 512;
    usb_cfg.tx_buffer_size = 1024;
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_cfg));
    usb_serial_jtag_vfs_use_driver();  // printf and the bridge share the driver instead of fighting over the port

    uart_config_t ucfg = {};
    ucfg.baud_rate = g_baud;
    ucfg.data_bits = UART_DATA_8_BITS;
    ucfg.parity = UART_PARITY_DISABLE;
    ucfg.stop_bits = UART_STOP_BITS_1;
    ucfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    ucfg.source_clk = UART_SCLK_DEFAULT;
    ESP_ERROR_CHECK(uart_driver_install(MODEM_UART, kUartBuf, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(MODEM_UART, &ucfg));
    ESP_ERROR_CHECK(uart_set_pin(MODEM_UART, MODEM_TX_GPIO, MODEM_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    char banner[256];
    std::snprintf(banner, sizeof banner,
                  "\nBRIDGE BEGIN modem AT bridge, firmware %s: UART2 TX=GPIO%d (D3) RX=GPIO%d (D1), %d baud 8N1, "
                  "no flow control; switch the modem on with its own button\n",
                  esp_app_get_description()->version, MODEM_TX_GPIO, MODEM_RX_GPIO, g_baud);
    say(banner);
    help();

    char line[kLineMax];
    size_t len = 0;
    uint8_t buf[128];
    for (;;) {
        // laptop -> modem, one line at a time
        const int n_usb = usb_serial_jtag_read_bytes(buf, sizeof buf, pdMS_TO_TICKS(10));
        for (int i = 0; i < n_usb; ++i) {
            const char c = static_cast<char>(buf[i]);
            if (c == '\r' || c == '\n') {
                if (len > 0) {
                    line[len] = '\0';
                    send_line(line, len);
                    len = 0;
                }
            } else if (c == '\b' || c == 0x7f) {
                if (len > 0) --len;
            } else if (len < kLineMax - 1) {
                line[len++] = c;
            }
        }
        // modem -> laptop, byte for byte
        const int n_uart = uart_read_bytes(MODEM_UART, buf, sizeof buf, pdMS_TO_TICKS(10));
        if (n_uart > 0) {
            usb_write(reinterpret_cast<const char*>(buf), static_cast<size_t>(n_uart));
            g_received += static_cast<uint32_t>(n_uart);
        }
    }
}
