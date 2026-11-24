// ICVSP ECU - the main file. Arduino calls setup() once after reset, then loop() forever.
// setup() starts the program chosen in config.h (smoke, report, camera or GNSS); their code is in src/app/.
// The Arduino IDE compiles this file together with every .cpp/.c file in src/ into one program.
#include <Arduino.h>

#include "config.h"
#include "src/app/ble_app.h"
#include "src/app/camera_test.h"
#include "src/app/gnss_app.h"
#include "src/app/log.h"
#include "src/app/report_app.h"
#include "src/app/smoke_test.h"

static const int LED_PIN = 21;  // the orange user LED, on when the pin is LOW

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);
    const uint32_t t = millis();
    while (!Serial && millis() - t < 4000) delay(10);  // give the Serial Monitor time to connect
    delay(500);
    log_init();

#if ICVSP_APP == ICVSP_APP_SMOKE
    run_smoke_test();   // src/app/smoke_test.cpp: prints SMOKE ... lines and the result
#elif ICVSP_APP == ICVSP_APP_REPORT
    start_report_app(); // src/app/report_app.cpp: runs in its own task, prints NET / DEMO / HTTP lines
#elif ICVSP_APP == ICVSP_APP_CAMERA
    run_camera_test();  // src/app/camera_test.cpp: prints CAM / CAMSTAT lines, sample photos and the result
#elif ICVSP_APP == ICVSP_APP_GNSS
    start_gnss_app();   // src/app/gnss_app.cpp: real L76K UART/NMEA bring-up, no Wi-Fi needed
#elif ICVSP_APP == ICVSP_APP_BLE
    start_ble_app();    // src/app/ble_app.cpp: BLE GATT server, runs in its own task
#else
#error "Set ICVSP_APP in config.h"
#endif
}

void loop() {
#if ICVSP_APP == ICVSP_APP_GNSS
    // Drain the GNSS UART promptly; the normal LED loop below blocks for seconds.
    gnss_app_loop();
    delay(1);
    static uint32_t last_led = 0;
    if (millis() - last_led >= 1000) {
        last_led = millis();
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
#if ICVSP_DEBUG
    static uint32_t gnss_last_debug = 0;
    if (millis() - gnss_last_debug >= ICVSP_DEBUG_EVERY_S * 1000UL) {
        gnss_last_debug = millis();
        where_am_i();
    }
#endif
    return;
#endif
#if ICVSP_DEBUG
    // Every few seconds: print where the program is right now.
    static uint32_t last_debug = 0;
    if (millis() - last_debug >= ICVSP_DEBUG_EVERY_S * 1000UL) {
        last_debug = millis();
        where_am_i();
#if ICVSP_APP == ICVSP_APP_REPORT
        report_status();
#endif
    }
#endif

#if ICVSP_APP == ICVSP_APP_SMOKE
    // Slow blink: all checks passed. Fast blink: something failed (see the Serial Monitor).
    const int ms = smoke_failures() == 0 ? 1000 : 150;
#elif ICVSP_APP == ICVSP_APP_CAMERA
    const int ms = camera_failures() == 0 ? 1000 : 150;
#else
    const int ms = 1000;  // heartbeat: the board is running
#endif
    digitalWrite(LED_PIN, LOW);
    delay(ms);
    digitalWrite(LED_PIN, HIGH);
    delay(ms);
}