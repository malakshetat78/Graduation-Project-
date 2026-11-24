# ECU pin map: Seeed XIAO ESP32-S3 Sense

Source of truth in code: [`firmware/ecu/main/board_pins.h`](../../../xaio_sw/ecu/main/board_pins.h). The firmware build
refuses to compile if two functions share a GPIO, or if a strapping, USB, flash or PSRAM pin is used
(`static_assert` at the end of that file).

**Status: nothing here has been checked on a real board yet.** The camera pins are fixed by the Sense board
(Seeed's published map); the GPS pads were confirmed by Malak; the IMU pins are a placeholder until Malak
decides them. Each row says how it will be checked when the board arrives (smoke test, section 5 of the
checklist).

| Function | XIAO pad | GPIO | Bus / setting | Source | Status | Checked on the board by |
|---|---|---|---|---|---|---|
| GPS L76K TX from ESP32 (-> L76K RX) | D6 | 43 | UART1, 9600 8N1 | Malak, 2026-11-15 | confirmed pads, direction unchecked | smoke test `gps`: NMEA bytes arrive; if none, swap TX/RX in `board_pins.h` |
| GPS L76K RX to ESP32 (<- L76K TX) | D7 | 44 | UART1 | Malak | as above | as above |
| IMU MPU6050 SDA | D4 | 5 | I2C port 1, 400 kHz, address 0x68 (AD0 low) or 0x69 | placeholder (XIAO's default I2C pads) | **PENDING** (Malak) | smoke test `imu`: WHO_AM_I answers at 0x68/0x69 |
| IMU MPU6050 SCL | D5 | 6 | I2C port 1 | placeholder | **PENDING** | as above |
| Camera OV3660 XCLK | - (Sense board) | 10 | 20 MHz | Seeed camera map (`CAMERA_MODEL_XIAO_ESP32S3`) | fixed by hardware | smoke test `camera`: sensor ID read, one frame captured |
| Camera SCCB SDA / SCL | - | 40 / 39 | I2C port 0 (`CONFIG_SCCB_HARDWARE_I2C_PORT0`) | Seeed | fixed | as above |
| Camera D7..D0 (Y9..Y2) | - | 48, 11, 12, 14, 16, 18, 17, 15 | parallel DVP | Seeed | fixed | as above |
| Camera VSYNC / HREF / PCLK | - | 38 / 47 / 13 | | Seeed | fixed | as above |
| Camera PWDN / RESET | - | not connected | | Seeed | fixed | - |
| User LED (yellow, active low) | - | 21 | GPIO | Seeed | fixed | smoke test blinks it |
| Reserved: 4G modem TX / RX / PWRKEY | D0 / D1 / D3 | 1 / 2 / 4 | UART2 | proposal, modem not bought | reserved | - |
| Console | USB-C | 19 / 20 | USB-Serial/JTAG | `sdkconfig.defaults` | set | every boot log |

## Conflicts and things to watch

1. **GPS on the chip's UART0 pins (D6/D7).** The console is moved to USB so UART1 can use them, but the ROM
   bootloader still prints its first lines on GPIO43 at every reset. The L76K just sees a few bytes of noise.
2. **Camera and IMU on separate I2C ports** (camera SCCB on port 0, IMU on port 1), so a slow or stuck IMU
   cannot block the camera. If Malak puts the IMU on the camera's SCCB lines instead, `IMU_I2C_PORT` must be
   port 0 and the camera driver shares it.
3. **GPIO21 is the user LED and also the microSD card's chip select on the Sense board.** The ECU does not use
   the microSD card; if it ever does, stop driving the LED.
4. **D8/D9/D10 (GPIO7/8/9)** are the microSD SPI lines on the Sense board: kept free.
5. **GPIO41/42** are the Sense board's PDM microphone: kept free.
6. **Strapping pins** GPIO0, 3 (D2), 45, 46 are avoided; GPIO26-37 belong to the flash and the octal PSRAM.
7. **Stacking:** the L76K module for XIAO sits on the pin headers. Check whether it also uses any pad besides
   D6/D7 (for example a reset or 1PPS line) before wiring the IMU to D4/D5.
8. **Power:** the MPU6050 breakout and the L76K run at 3.3 V from the XIAO's 3V3 pin; check the breakout has
   no 5 V-only regulator or pull-ups to 5 V.

## Open questions for Malak

- Which pads the MPU6050 is wired to (then update `IMU_SDA_GPIO` / `IMU_SCL_GPIO` and this table).
- MPU6050 AD0 pin level (address 0x68 or 0x69) and whether its INT pin is wired (not used yet).
- Whether the L76K module uses any extra pad.
