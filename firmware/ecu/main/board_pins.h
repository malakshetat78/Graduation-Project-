#pragma once
// Every pin the ECU uses, in one place. Seeed XIAO ESP32-S3 Sense; "Dn" is the pad name printed on the board.
//
// Status of each group:
//   CAMERA  fixed by the Sense expansion board (Seeed camera pin map, CAMERA_MODEL_XIAO_ESP32S3).
//   GPS     Seeed "L76K GNSS Module for XIAO", stacked directly on the XIAO (Malak 2026-11-15). Its pads, from
//           Seeed's pinout V1.1: module RX on D6, TX on D7, WAKEUP on D0, RESET on D2, plus 3V3/GND/5V.
//           D1, D3, D4, D5, D8, D9, D10 pass straight through. The direction below matches Seeed's own example
//           (ESP32 TX on D6, RX on D7) and is fixed by the stack: if no NMEA arrives, do NOT swap TX/RX (both
//           sides would drive the same line); check the module is seated the right way, powered, and the baud.
//   IMU     MPU6050 on I2C, D4/D5 (confirmed by Malak's PCB and breadboard, 2026-11-18).
//   LED     on-board user LED (yellow), active low.

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"

// ---- GPS: L76K, NMEA 0183 at 9600 baud 8N1 -------------------------------------------------------------
// D6/D7 are the chip's UART0 pins. The console is moved to USB-Serial/JTAG so UART1 can use them; the ROM
// bootloader still prints its first lines on GPIO43 at reset (harmless for the receiver).
constexpr uart_port_t GPS_UART = UART_NUM_1;
constexpr int GPS_TX_GPIO = 43;  // D6: ESP32 TX -> L76K RX
constexpr int GPS_RX_GPIO = 44;  // D7: ESP32 RX <- L76K TX
constexpr int GPS_BAUD = 9600;
// Taken by the stacked module, never driven by this firmware. Both are pulled up inside the L76K, so they sit
// high once running (0 on WAKEUP = sleep, 0 on RESET = reset). ESP32-S3 GPIO1-4 can show a short low pulse
// (about 60 us) at power-up (Espressif hardware design guidelines): whether that resets or sleeps the L76K is
// unchecked; a cold power-up on the board (smoke test `gps` sends NMEA) is the check. Listed so nothing else
// can take them.
constexpr int GPS_WAKEUP_GPIO = 1;  // D0
constexpr int GPS_RESET_GPIO = 3;   // D2 (a strapping pin, but only held by the module's pull-up: harmless at boot)

// ---- IMU: MPU6050 ---------------------------------------------------------------------------------------
constexpr i2c_port_num_t IMU_I2C_PORT = I2C_NUM_1;  // port 0 is used by the camera (SCCB)
constexpr int IMU_SDA_GPIO = 5;                     // D4
constexpr int IMU_SCL_GPIO = 6;                     // D5
constexpr uint32_t IMU_I2C_HZ = 400000;

// ---- Camera: OV3660 on the Sense board (fixed) ---------------------------------------------------------
constexpr int CAM_PWDN = -1;
constexpr int CAM_RESET = -1;
constexpr int CAM_XCLK = 10;
constexpr int CAM_SIOD = 40;  // SCCB data
constexpr int CAM_SIOC = 39;  // SCCB clock
constexpr int CAM_Y9 = 48;
constexpr int CAM_Y8 = 11;
constexpr int CAM_Y7 = 12;
constexpr int CAM_Y6 = 14;
constexpr int CAM_Y5 = 16;
constexpr int CAM_Y4 = 18;
constexpr int CAM_Y3 = 17;
constexpr int CAM_Y2 = 15;
constexpr int CAM_VSYNC = 38;
constexpr int CAM_HREF = 47;
constexpr int CAM_PCLK = 13;
constexpr int CAM_SCCB_PORT = 0;  // must match CONFIG_SCCB_HARDWARE_I2C_PORT0 in sdkconfig.defaults

// ---- Mobile modem (fallback uplink): UART2 ---------------------------------------------------------------
// Candidates: SIM900A (2G, the mentor's module; bring-up plan in results/bringup/SIM900A_BRINGUP.md) or a 4G
// module (SIM7600E-H or a Cat-1 module). Only the AT bridge firmware (modem_bridge_main.cpp) uses these so far.
// With the GPS stacked, only D1 and D3 are free: D0/D2 are the GPS's WAKEUP/RESET, D4/D5 the IMU, D6/D7 the GPS
// UART, D8/D9/D10 the Sense board's microSD. No power-key pin is left: use the breakout's own power button.
// The modem needs its own 5 V / 2 A supply (common ground), never the XIAO's 3V3 or 5V pin. The SIM900's own
// UART runs at 2.8 V: check the breakout's level shifting before wiring it to the 3.3 V ESP32.
constexpr uart_port_t MODEM_UART = UART_NUM_2;
// GPIO4/GPIO2 can also pulse low for about 60 us at power-up: a powered modem may see one garbage byte (send
// "AT" a few times). Neither is a power-key line, so the modem cannot be switched by it.
constexpr int MODEM_TX_GPIO = 4;  // D3: ESP32 TX -> modem RX
constexpr int MODEM_RX_GPIO = 2;  // D1: ESP32 RX <- modem TX

// ---- User LED -------------------------------------------------------------------------------------------
constexpr gpio_num_t LED_GPIO = GPIO_NUM_21;  // low = on

// ---- Build-time check: no GPIO is given to two functions --------------------------------------------------
// (the modem and the GPS's WAKEUP/RESET pads are included so nothing else can take them by accident)
namespace board_pins_check {
constexpr int kUsed[] = {GPS_TX_GPIO, GPS_RX_GPIO, IMU_SDA_GPIO, IMU_SCL_GPIO, CAM_XCLK, CAM_SIOD, CAM_SIOC,
                         CAM_Y9, CAM_Y8, CAM_Y7, CAM_Y6, CAM_Y5, CAM_Y4, CAM_Y3, CAM_Y2, CAM_VSYNC, CAM_HREF,
                         CAM_PCLK, static_cast<int>(LED_GPIO), MODEM_TX_GPIO, MODEM_RX_GPIO, GPS_WAKEUP_GPIO,
                         GPS_RESET_GPIO};
constexpr bool all_distinct() {
    for (size_t i = 0; i < sizeof kUsed / sizeof kUsed[0]; ++i)
        for (size_t j = i + 1; j < sizeof kUsed / sizeof kUsed[0]; ++j)
            if (kUsed[i] == kUsed[j]) return false;
    return true;
}
constexpr bool none_is_strapping_or_flash() {  // GPIO0/3/45/46 strapping, 19/20 USB, 26-37 flash + octal PSRAM
    for (int g : kUsed)
        if (g == 3 && g == GPS_RESET_GPIO) continue;  // only GPIO3 as the stacked GPS's RESET: fixed by the module
        else if (g == 0 || g == 3 || g == 45 || g == 46 || g == 19 || g == 20 || (g >= 26 && g <= 37)) return false;
    return true;
}
static_assert(all_distinct(), "board_pins.h: a GPIO is assigned to two functions");
static_assert(none_is_strapping_or_flash(), "board_pins.h: a GPIO is a strapping, USB, flash or PSRAM pin");
static_assert(GPS_RESET_GPIO == 3, "board_pins.h: the stacked L76K's RESET is D2 (GPIO3); the strapping exception is only for it");
}  // namespace board_pins_check
