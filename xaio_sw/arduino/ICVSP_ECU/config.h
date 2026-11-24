#pragma once
// ===== ICVSP ECU settings: the only file you normally change (plus secrets.h for Wi-Fi) =====

// Which program runs after the board starts. Pick ONE, then Upload again:
//   ICVSP_APP_SMOKE   hardware check: chip, PSRAM, signing, GPS, IMU, camera (no Wi-Fi needed)
//   ICVSP_APP_REPORT  report firmware: Wi-Fi -> time -> device key -> signed report -> AWS
//   ICVSP_APP_CAMERA  T-012 camera bring-up: 20 photos x 3 sizes, timing, memory, failures (no Wi-Fi needed)
//   ICVSP_APP_BLE     BLE GATT server (Mariam): sends simulated reports to the phone, needs NimBLE-Arduino 2.x
//   ICVSP_APP_GNSS    T-014 L76K GNSS UART bring-up: position/time/fix/stale/disconnect (no Wi-Fi needed)
#define ICVSP_APP_SMOKE  1
#define ICVSP_APP_REPORT 2
#define ICVSP_APP_CAMERA 3
// 4 is kept free for the IMU (Rana) program, added in its own PR.
#define ICVSP_APP_BLE    5
#define ICVSP_APP_GNSS   6
#define ICVSP_APP ICVSP_APP_SMOKE
// ---- debug prints: turn ON or OFF here ----
//   1 = ON:  prints ">>> STEP n/N: ..." when each step starts, and "[NOW] ..." every ICVSP_DEBUG_EVERY_S
//            seconds, so you always see where the program is right now.
//   0 = OFF: only the normal SMOKE / NET / DEMO / HTTP lines.
// Change the number, then Upload again.
#define ICVSP_DEBUG 1
#define ICVSP_DEBUG_EVERY_S 5

// ---- L76K GNSS bring-up (T-014) ----
// Defaults from existing smoke_test.cpp; verify wiring before applying power.
// Module TX --> ESP32 RX D7/GPIO44; module RX <-- ESP32 TX D6/GPIO43.
#define CONFIG_ICVSP_GNSS_RX_PIN 44
#define CONFIG_ICVSP_GNSS_TX_PIN 43
#define CONFIG_ICVSP_GNSS_BAUD 9600
#define CONFIG_ICVSP_GNSS_PRINT_EVERY_MS 1000
// Opt-in only: USB S/D simulate stale/UART loss for 6 s in GNSS mode.
#ifndef CONFIG_ICVSP_GNSS_SOFTWARE_TESTS
#define CONFIG_ICVSP_GNSS_SOFTWARE_TESTS 0
#endif

// ---- smoke test ----
#define CONFIG_ICVSP_GPS_LISTEN_S 5          // seconds to listen to the GPSr


// ---- camera bring-up test (T-012), ICVSP_APP_CAMERA ----
// Write the light before each run, it is printed in the log and the photo headers:
// "bright room", "dim room", "lens covered", "outdoor daylight" ...
#define CONFIG_ICVSP_CAM_LIGHTING "not recorded"
#define CONFIG_ICVSP_CAM_SHOTS 20              // photos per resolution
#define CONFIG_ICVSP_CAM_WARMUP_FRAMES 3       // frames thrown away after each start (exposure settling)
#define CONFIG_ICVSP_CAM_INIT_ATTEMPTS 3       // camera start retries before the start counts as failed
#define CONFIG_ICVSP_CAM_MIN_JPEG_BYTES 512    // smaller frames count as broken
#define CONFIG_ICVSP_CAM_LEAK_TOLERANCE 4096   // bytes of RAM allowed to be missing after the camera stops
#define CONFIG_ICVSP_CAM_DUMP_JPEG 1           // 1 = print one QVGA and one VGA photo as Base64 for extract_jpegs.py

// ---- report firmware ----
// The team backend on AWS (POST /reports).
#define CONFIG_ICVSP_REPORT_URL "https://bamrqyu5hizs6q6dy37wxvlebu0xwtlk.lambda-url.us-east-1.on.aws/reports"
#define CONFIG_ICVSP_SNTP_SERVER "pool.ntp.org"
// Simulated drive position: the team test location in the Western Desert (same as the ESP-IDF AWS build).
#define CONFIG_ICVSP_DEMO_LAT "23.5"
#define CONFIG_ICVSP_DEMO_LON "27.0"
// Empty = device ID made from the chip MAC address: ecu-<12 hex digits>. Leave it empty.
#define CONFIG_ICVSP_DEVICE_ID ""
// The signing key is made on the chip at first boot and kept in flash (NVS). Never use a demo seed here.
#define CONFIG_ICVSP_KEY_DEMO_SEED 0
// The Arduino ESP32 core is built without TLS 1.3, so this must stay 0 here (the ESP-IDF build uses 1.3).
#define CONFIG_ICVSP_TLS_REQUIRE_1_3 0