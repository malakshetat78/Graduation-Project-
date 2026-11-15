#pragma once
// Every pin the ECU uses, in one place. Seeed XIAO ESP32-S3 Sense; "Dn" is the pad name printed on the board.
//
// Status of each group:
//   CAMERA  fixed by the Sense expansion board (Seeed camera pin map, CAMERA_MODEL_XIAO_ESP32S3).
//   GPS     L76K module stacked on D6/D7 (confirmed by Malak 2026-11-15). Direction (which pad is TX)
//           to be checked on the board: if no NMEA arrives, swap GPS_TX_GPIO and GPS_RX_GPIO.
//   IMU     MPU6050 on I2C. PENDING: pins not decided yet (Malak). D4/D5 is a placeholder.
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

// ---- IMU: MPU6050 (PENDING pins) -----------------------------------------------------------------------
constexpr i2c_port_num_t IMU_I2C_PORT = I2C_NUM_1;  // port 0 is used by the camera (SCCB)
constexpr int IMU_SDA_GPIO = 5;                     // D4  PENDING
constexpr int IMU_SCL_GPIO = 6;                     // D5  PENDING
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

// ---- Reserved: 4G modem (SIM7600E-H or a Cat-1 module), proposed, not bought yet -------------------------
// UART2 on D0 (GPIO1, ESP32 TX -> modem RX) and D1 (GPIO2, ESP32 RX <- modem TX); D3 (GPIO4) for the modem's
// power-key input. D8/D9/D10 are shared with the Sense board's microSD; GPIO3 (D2) is a strapping pin.
// The modem board needs its own 5 V feed from the car supply (common ground) and a 3.3 V UART level.

// ---- User LED -------------------------------------------------------------------------------------------
constexpr gpio_num_t LED_GPIO = GPIO_NUM_21;  // low = on
