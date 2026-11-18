# ECU pin map: Seeed XIAO ESP32-S3 Sense

Source of truth in code: [`firmware/ecu/main/board_pins.h`](../../ecu/main/board_pins.h). The firmware build
refuses to compile if two functions share a GPIO, or if a strapping, USB, flash or PSRAM pin is used
(`static_assert` at the end of that file). The one exception is GPIO3 (D2): the stacked GPS module's own RESET
line, held high by the module and never driven by the firmware.

**Status: nothing here has been checked on a real board yet.** The camera pins are fixed by the Sense board
(Seeed's published map); the GPS pads were confirmed by Malak and by Seeed's module pinout; the IMU pins are
confirmed by Malak's PCB and breadboard diagram (2026-11-18). Each row says how it will be checked when the board arrives (smoke test, section 5 of the
checklist).

| Function | XIAO pad | GPIO | Bus / setting | Source | Status | Checked on the board by |
|---|---|---|---|---|---|---|
| GPS L76K TX from ESP32 (-> L76K RX) | D6 | 43 | UART1, 9600 8N1 | Malak, 2026-11-15; Seeed example (TX=D6, RX=D7) | confirmed, direction fixed by the stack | smoke test `gps`: NMEA bytes arrive; if none, check seating/orientation/power (never swap TX/RX) |
| GPS L76K RX to ESP32 (<- L76K TX) | D7 | 44 | UART1 | Malak | as above | as above |
| GPS L76K WAKEUP (0 = sleep) | D0 | 1 | not driven; pulled up inside the L76K (ESP32-S3 may pulse it low ~60 us at power-up: check on a cold boot) | Seeed module pinout V1.1 (stacked) | taken by the module | GPS keeps sending NMEA |
| GPS L76K RESET (0 = reset) | D2 | 3 | not driven; pulled up inside the L76K (strapping pin, held high: harmless) | Seeed | taken by the module | as above |
| IMU MPU6050 SDA | D4 | 5 | I2C port 1, 400 kHz, address 0x68 (AD0 low) or 0x69 | Malak's PCB + breadboard, 2026-11-18 | confirmed | smoke test `imu`: WHO_AM_I answers at 0x68/0x69 |
| IMU MPU6050 SCL | D5 | 6 | I2C port 1 | Malak | confirmed | as above |
| IMU MPU6050 VCC / GND | 3V3 / GND | - | | Malak | confirmed | as above |
| Camera OV3660 XCLK | - (Sense board) | 10 | 20 MHz | Seeed camera map (`CAMERA_MODEL_XIAO_ESP32S3`) | fixed by hardware | smoke test `camera`: sensor ID read, one frame captured |
| Camera SCCB SDA / SCL | - | 40 / 39 | I2C port 0 (`CONFIG_SCCB_HARDWARE_I2C_PORT0`) | Seeed | fixed | as above |
| Camera D7..D0 (Y9..Y2) | - | 48, 11, 12, 14, 16, 18, 17, 15 | parallel DVP | Seeed | fixed | as above |
| Camera VSYNC / HREF / PCLK | - | 38 / 47 / 13 | | Seeed | fixed | as above |
| Camera PWDN / RESET | - | not connected | | Seeed | fixed | - |
| User LED (yellow, active low) | - | 21 | GPIO | Seeed | fixed | smoke test blinks it |
| Reserved: mobile modem TX / RX (SIM900A first) | D3 / D1 | 4 / 2 | UART2; no power-key pin left | the only pads the stacked GPS leaves free | reserved | AT bridge (`SIM900A_BRINGUP.md`) |
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
6. **Strapping pins** GPIO0, 45, 46 are avoided; GPIO3 (D2) is the stacked GPS's RESET line, held high by
   the module's pull-up and never driven by the firmware. GPIO26-37 belong to the flash and the octal PSRAM.
7. **Stacking:** the L76K module for XIAO uses D6/D7 (UART), D0 (WAKEUP) and D2 (RESET), plus 3V3/GND/5V
   (Seeed pinout V1.1). D1, D3, D4, D5, D8, D9, D10 pass through. So the modem is on D3/D1, not D0. Plug the
   module the right way round: Seeed warns a reversed module can burn it or the XIAO.
8. **Power:** the MPU6050 breakout and the L76K run at 3.3 V from the XIAO's 3V3 pin; check the breakout has
   no 5 V-only regulator or pull-ups to 5 V.

## Open questions

- MPU6050 AD0 is not wired in Malak's design: the address depends on the breakout (most pull it low: 0x68).
  The smoke test tries both 0x68 and 0x69, so either works. INT is not wired (not used).
