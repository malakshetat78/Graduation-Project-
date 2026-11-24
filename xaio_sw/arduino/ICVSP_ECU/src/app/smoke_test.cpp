// Hardware smoke test for the XIAO ESP32-S3 Sense with the L76K GPS board and the MPU6050.
// Every check prints one line: SMOKE <check> PASS/FAIL <details>, then SMOKE RESULT pass=N fail=M.
#include "smoke_test.h"

#include <Arduino.h>
#include <Wire.h>

#include "../../config.h"
#include "../core/signer.h"
#include "esp_camera.h"
#include "esp_random.h"
#include "log.h"

namespace {

const int GPS_RX = 44, GPS_TX = 43;  // D7 <- L76K TX, D6 -> L76K RX (9600 baud)
const int IMU_SDA = 5, IMU_SCL = 6;  // D4, D5. IMU on I2C port 0 (Wire); in the Arduino core the camera takes port 1

int g_pass = 0, g_fail = 0;

void result(const char* name, bool ok, const String& detail) {
    log_line("SMOKE %-7s %s  %s\n", name, ok ? "PASS" : "FAIL", detail.c_str());
    ok ? g_pass++ : g_fail++;
}

// 1. Chip and memory
void check_chip() {
    result("chip", true, String(ESP.getChipModel()) + " rev" + ESP.getChipRevision() + " cores=" +
                             ESP.getChipCores() + " flash=" + (ESP.getFlashChipSize() / (1024 * 1024)) + "MB");
    const bool psram = psramFound();
    result("psram", psram, psram ? String(ESP.getPsramSize() / 1024) + " KB"
                                 : String("not found: Tools > PSRAM > OPI PSRAM"));
}

// 2. Signing on the chip (the same icvsp_core + Monocypher code the report firmware uses).
// The key is random, in RAM for this run only, never stored or sent.
void check_signing() {
    uint8_t seed[32], sk[64], pk[32], sig[64];
    esp_fill_random(seed, sizeof seed);
    icvsp::keypair_from_seed(seed, sk, pk);  // wipes the seed
    const icvsp::MessageHeader h{"ecu-smoke", "smoke-1", 1794736800000LL};
    uint8_t payload[] = "{\"type\":\"smoke-test\"}";
    const size_t len = sizeof payload - 1;
    const uint32_t t0 = micros();
    const bool signed_ok = icvsp::sign_message(sk, h, payload, len, sig);
    const uint32_t t1 = micros();
    const bool verifies = signed_ok && icvsp::verify_message(pk, h, payload, len, sig);
    const uint32_t t2 = micros();
    payload[3] ^= 1;  // change one byte: the signature must no longer verify
    const bool tamper_rejected = !icvsp::verify_message(pk, h, payload, len, sig);
    memset(sk, 0, sizeof sk);
    result("signing", signed_ok && verifies && tamper_rejected,
           String("sign=") + (signed_ok ? "ok" : "FAILED") + " verify=" + (verifies ? "ok" : "FAILED") +
               " tampered_rejected=" + (tamper_rejected ? "yes" : "NO") + " sign_ms=" + String((t1 - t0) / 1000.0, 1) +
               " verify_ms=" + String((t2 - t1) / 1000.0, 1));
}

// 3. GPS: count NMEA sentences with a good checksum
void check_gps() {
    Serial1.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
    uint32_t start = millis(), bytes = 0, good = 0, bad = 0;
    bool fix = false;
    String line;
    while (millis() - start < CONFIG_ICVSP_GPS_LISTEN_S * 1000UL) {
        while (Serial1.available()) {
            const char c = Serial1.read();
            bytes++;
            if (c == '$') {
                line = "$";
            } else if (c == '\n' && line.length() > 6) {
                line.trim();
                const int star = line.lastIndexOf('*');
                if (star > 0 && star + 2 < (int)line.length()) {
                    uint8_t sum = 0;
                    for (int i = 1; i < star; i++) sum ^= line[i];
                    const uint8_t want = strtoul(line.substring(star + 1, star + 3).c_str(), nullptr, 16);
                    if (sum == want) {
                        good++;
                        if ((line.startsWith("$GNRMC") || line.startsWith("$GPRMC")) && line.indexOf(",A,") > 0) fix = true;
                    } else {
                        bad++;
                    }
                }
                line = "";
            } else if (line.length() > 0 && line.length() < 120) {
                line += c;
            }
        }
        delay(5);
    }
    String d = String(bytes) + " bytes, " + good + " good NMEA, " + bad + " bad, fix=" + (fix ? "yes" : "no (normal indoors)");
    if (bytes == 0) d += "  -> no bytes: GPS board seated? D6/D7?";
    result("gps", good > 0, d);
}

// 4. IMU (MPU6050): found on I2C, WHO_AM_I = 0x68, about 1 g while lying still
bool imu_read(uint8_t addr, uint8_t reg, uint8_t* buf, int n) {
    Wire.beginTransmission(addr);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((int)addr, n) != n) return false;
    for (int i = 0; i < n; i++) buf[i] = Wire.read();
    return true;
}

void check_imu() {
    Wire.begin(IMU_SDA, IMU_SCL, 400000);
    uint8_t addr = 0;
    for (uint8_t a : {0x68, 0x69}) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            addr = a;
            break;
        }
    }
    if (!addr) {
        Wire.end();
        result("imu", false, "no I2C answer at 0x68/0x69: SDA=D4 SCL=D5 VCC=3V3 GND?");
        return;
    }
    uint8_t who = 0;
    imu_read(addr, 0x75, &who, 1);
    Wire.beginTransmission(addr);  // wake up (register 0x6B = 0)
    Wire.write(0x6B);
    Wire.write(0x00);
    Wire.endTransmission();
    delay(100);
    float sum = 0;
    int ok = 0;
    for (int i = 0; i < 20; i++) {
        uint8_t b[6];
        if (imu_read(addr, 0x3B, b, 6)) {
            const float x = (int16_t)(b[0] << 8 | b[1]) / 16384.0f;
            const float y = (int16_t)(b[2] << 8 | b[3]) / 16384.0f;
            const float z = (int16_t)(b[4] << 8 | b[5]) / 16384.0f;
            sum += sqrtf(x * x + y * y + z * z);
            ok++;
        }
        delay(10);
    }
    const float g = ok ? sum / ok : 0;
    Wire.end();  // free the I2C bus again
    result("imu", who == 0x68 && ok == 20 && g > 0.8f && g < 1.2f,
           String("addr=0x") + String(addr, HEX) + " WHO_AM_I=0x" + String(who, HEX) + " |a|=" + String(g, 2) +
               " g (want about 1.00, board lying still)");
}

// 5. Camera (OV3660): starts, takes a QVGA JPEG with valid start and end markers
void check_camera() {
    camera_config_t c = {};
    c.pin_pwdn = -1;
    c.pin_reset = -1;
    c.pin_xclk = 10;
    c.pin_sccb_sda = 40;
    c.pin_sccb_scl = 39;
    c.pin_d7 = 48; c.pin_d6 = 11; c.pin_d5 = 12; c.pin_d4 = 14;
    c.pin_d3 = 16; c.pin_d2 = 18; c.pin_d1 = 17; c.pin_d0 = 15;
    c.pin_vsync = 38;
    c.pin_href = 47;
    c.pin_pclk = 13;
    c.xclk_freq_hz = 20000000;
    c.ledc_timer = LEDC_TIMER_0;
    c.ledc_channel = LEDC_CHANNEL_0;
    c.pixel_format = PIXFORMAT_JPEG;
    c.frame_size = FRAMESIZE_QVGA;
    c.jpeg_quality = 12;
    c.fb_count = 1;
    c.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
    c.grab_mode = CAMERA_GRAB_LATEST;
    const esp_err_t err = esp_camera_init(&c);
    if (err != ESP_OK) {
        result("camera", false, String("init failed 0x") + String(err, HEX) + ": camera ribbon seated?");
        return;
    }
    sensor_t* s = esp_camera_sensor_get();
    const uint16_t pid = s ? s->id.PID : 0;
    camera_fb_t* fb = nullptr;
    for (int i = 0; i < 3; i++) {  // the first frames can be dark: keep the third
        if (fb) esp_camera_fb_return(fb);
        fb = esp_camera_fb_get();
    }
    const bool jpeg = fb && fb->len > 4 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8 &&
                      fb->buf[fb->len - 2] == 0xFF && fb->buf[fb->len - 1] == 0xD9;
    const String d = String("sensor PID=0x") + String(pid, HEX) + (pid == 0x3660 ? " (OV3660)" : " (expected OV3660)") +
                     ", frame " + (fb ? String(fb->width) + "x" + fb->height + " " + fb->len + " bytes" : String("none"));
    if (fb) esp_camera_fb_return(fb);
    esp_camera_deinit();
    result("camera", jpeg, d);
}

}  // namespace

void run_smoke_test() {
    log_line("\nSMOKE BEGIN ICVSP ECU smoke test (Arduino build)\n");
    step(1, 6, "chip and PSRAM memory");
    check_chip();
    step(2, 6, "signing (make a test key, sign, verify, tamper)");
    check_signing();
    step(3, 6, "GPS: listening on D7/D6 at 9600 baud");
    check_gps();
    step(4, 6, "IMU: MPU6050 on D4/D5, keep the board still");
    check_imu();
    step(5, 6, "camera: start it and take a photo");
    check_camera();
    step(6, 6, g_fail == 0 ? "finished: all passed" : "finished: something FAILED (see the FAIL lines above)");
    log_line("SMOKE RESULT pass=%d fail=%d\n", g_pass, g_fail);
    log_line("%s\n", g_fail == 0 ? "LED: slow blink = all passed" : "LED: fast blink = something failed");
}

int smoke_failures() { return g_fail; }
