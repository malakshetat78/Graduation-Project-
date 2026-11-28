// ICVSP ECU boot/sensor smoke test (Seeed XIAO ESP32-S3 Sense).
//
// Runs each check once, on its own, so one failure does not hide the others, then prints one line per check
//     SMOKE <check> PASS|FAIL|SKIP <details>
// and a summary line "SMOKE RESULT pass=.. fail=.. skip=..". After that the user LED blinks slowly if
// nothing failed and fast if something did, so the result is visible without a laptop, and the GPS keeps
// being read with a "SMOKE gps-live" line every 10 s (to watch the first fix outdoors).
//
// Privacy: the GPS position and camera pixels are never printed (logs go into git); only counts, sizes and IDs.
// Security: the signing check uses a random key made in RAM for this run only, never stored or sent.
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "board_pins.h"
#include "esp_app_desc.h"
#include "esp_camera.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_idf_version.h"
#include "esp_psram.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nmea.h"
#include "sdkconfig.h"
#include "signer.h"

namespace {

enum class Result { Pass, Fail, Skip };

struct Check {
    const char* name;
    Result result = Result::Skip;
    char detail[200] = {};
};

void set(Check& c, Result r, const char* fmt, ...) {
    c.result = r;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c.detail, sizeof c.detail, fmt, ap);
    va_end(ap);
}

const char* text(Result r) { return r == Result::Pass ? "PASS" : r == Result::Fail ? "FAIL" : "SKIP"; }

uint32_t now_ms() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

const char* reset_reason_text(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON: return "power-on";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "panic";
        case ESP_RST_INT_WDT: return "interrupt watchdog";
        case ESP_RST_TASK_WDT: return "task watchdog";
        case ESP_RST_BROWNOUT: return "brownout";
        case ESP_RST_USB: return "usb";
        case ESP_RST_JTAG: return "jtag";
        default: return "other";
    }
}

// ---- 1. Chip, flash, PSRAM -----------------------------------------------------------------------------
void check_chip(Check& c) {
    esp_chip_info_t ci;
    esp_chip_info(&ci);
    uint32_t flash = 0;
    esp_flash_get_size(nullptr, &flash);
    size_t psram = 0;
#if CONFIG_SPIRAM
    if (esp_psram_is_initialized()) psram = esp_psram_get_size();
#endif
    const bool wifi = ci.features & CHIP_FEATURE_WIFI_BGN;
    const bool ble = ci.features & CHIP_FEATURE_BLE;
    const bool ok = ci.model == CHIP_ESP32S3 && ci.cores == 2 && flash >= 8u * 1024 * 1024 && wifi && ble &&
                    psram >= 8u * 1024 * 1024;
    set(c, ok ? Result::Pass : Result::Fail,
        "model=%s cores=%d rev=v%d.%d wifi=%s ble=%s flash=%lu KB psram=%u KB (expect ESP32-S3, 2 cores, "
        "8192 KB flash, 8192 KB PSRAM)",
        ci.model == CHIP_ESP32S3 ? "ESP32-S3" : "other", ci.cores, ci.revision / 100, ci.revision % 100,
        wifi ? "yes" : "no", ble ? "yes" : "no", static_cast<unsigned long>(flash / 1024),
        static_cast<unsigned>(psram / 1024));
}

// ---- 2. Signing on the chip (icvsp_core + Monocypher) ------------------------------------------------
void check_signing(Check& c) {
    uint8_t seed[32], sk[64], pk[32], sig[64];
    esp_fill_random(seed, sizeof seed);
    icvsp::keypair_from_seed(seed, sk, pk);  // wipes the seed
    const icvsp::MessageHeader h{"ecu-smoke", "smoke-1", 1794736800000LL};
    uint8_t payload[] = "{\"type\":\"smoke-test\"}";
    const size_t len = sizeof payload - 1;

    const int64_t t0 = esp_timer_get_time();
    const bool signed_ok = icvsp::sign_message(sk, h, payload, len, sig);
    const int64_t t1 = esp_timer_get_time();
    const bool verifies = signed_ok && icvsp::verify_message(pk, h, payload, len, sig);
    const int64_t t2 = esp_timer_get_time();
    payload[3] ^= 1;  // tamper with one byte
    const bool tamper_rejected = !icvsp::verify_message(pk, h, payload, len, sig);
    memset(sk, 0, sizeof sk);

    set(c, signed_ok && verifies && tamper_rejected ? Result::Pass : Result::Fail,
        "sign=%s verify=%s tampered_rejected=%s sign_ms=%.1f verify_ms=%.1f (RAM-only random key)",
        signed_ok ? "ok" : "FAILED", verifies ? "ok" : "FAILED", tamper_rejected ? "yes" : "NO",
        (t1 - t0) / 1000.0, (t2 - t1) / 1000.0);
}

// ---- 3. GPS: bytes and NMEA sentences on UART1 -------------------------------------------------------
// Kept open after the check: once the results are printed, the GPS keeps being read and a status line is
// printed every 10 s, so the first fix can be watched outdoors (a cold start can take minutes).
struct GpsMonitor {
    icvsp::NmeaParser gnss;
    uint32_t bytes = 0, rmc = 0, gga = 0, other = 0;
    char type[6] = {};
    int type_len = -1;  // -1: not inside a sentence header
    bool ready = false;
    uint32_t first_fix_ms = 0;  // device time of the first valid fix this boot (0: none yet); never reset
    bool first_fix_printed = false;

    void read_for(uint32_t ms) {
        uint8_t buf[128];
        const uint32_t start = now_ms();
        while (now_ms() - start < ms) {
            const int n = uart_read_bytes(GPS_UART, buf, sizeof buf, pdMS_TO_TICKS(100));
            for (int i = 0; i < n; ++i) feed(static_cast<char>(buf[i]));
            if (n > 0) bytes += static_cast<uint32_t>(n);
            // checked after every chunk read, so a fix that lasts only a moment is still caught
            if (n > 0 && !first_fix_ms && gnss.fix(now_ms()).valid) first_fix_ms = now_ms() | 1u;  // never 0
        }
    }

    void feed(char ch) {
        gnss.feed(ch, now_ms());
        // count sentence types from the header "$GNRMC," (talker ignored)
        if (ch == '$') {
            type_len = 0;
        } else if (type_len >= 0) {
            if (ch == ',' || type_len == 5) {
                type[type_len] = '\0';
                if (type_len == 5 && strcmp(type + 2, "RMC") == 0) ++rmc;
                else if (type_len == 5 && strcmp(type + 2, "GGA") == 0) ++gga;
                else ++other;
                type_len = -1;
            } else {
                type[type_len++] = ch;
            }
        }
    }
};

GpsMonitor g_gps;

void check_gps(Check& c) {
    uart_config_t cfg = {};
    cfg.baud_rate = GPS_BAUD;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;
    esp_err_t e = uart_driver_install(GPS_UART, 2048, 0, 0, nullptr, 0);
    if (e == ESP_OK) e = uart_param_config(GPS_UART, &cfg);
    if (e == ESP_OK) e = uart_set_pin(GPS_UART, GPS_TX_GPIO, GPS_RX_GPIO, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (e != ESP_OK) {
        set(c, Result::Fail, "UART setup failed: %s", esp_err_to_name(e));
        return;
    }
    g_gps.ready = true;
    g_gps.read_for(CONFIG_ICVSP_GPS_LISTEN_S * 1000u);

    const auto& st = g_gps.gnss.stats();
    const icvsp::GnssFix fix = g_gps.gnss.fix(now_ms());
    const Result r = st.sentences > 0 ? Result::Pass : Result::Fail;
    set(c, r,
        "listened %d s: bytes=%lu good_sentences=%lu checksum_errors=%lu RMC=%lu GGA=%lu other=%lu sats=%u "
        "utc_known=%s fix=%s%s",
        CONFIG_ICVSP_GPS_LISTEN_S, (unsigned long)g_gps.bytes, (unsigned long)st.sentences,
        (unsigned long)st.checksum_errors, (unsigned long)g_gps.rmc, (unsigned long)g_gps.gga,
        (unsigned long)g_gps.other, g_gps.gnss.satellites(), g_gps.gnss.has_time() ? "yes" : "no",
        fix.valid ? "yes" : "no (normal indoors)",
        g_gps.bytes == 0 ? " - no bytes: check wiring, or swap GPS_TX_GPIO/GPS_RX_GPIO" : "");
}

// After the summary: GPS status every 10 s, and one line (once per boot) for the first fix. Never returns.
[[noreturn]] void watch_gps_forever() {
    for (;;) {
        g_gps.read_for(10000);
        const icvsp::GnssFix fix = g_gps.gnss.fix(now_ms());
        const auto& st = g_gps.gnss.stats();
        printf("SMOKE gps-live t=%lus bytes=%lu good_sentences=%lu checksum_errors=%lu sats=%u utc_known=%s "
               "fix=%s\n",
               (unsigned long)(now_ms() / 1000), (unsigned long)g_gps.bytes, (unsigned long)st.sentences,
               (unsigned long)st.checksum_errors, g_gps.gnss.satellites(), g_gps.gnss.has_time() ? "yes" : "no",
               fix.valid ? "yes" : "no");
        if (g_gps.first_fix_ms && !g_gps.first_fix_printed) {
            g_gps.first_fix_printed = true;
            printf("SMOKE gps-fix first fix %lu s after boot (fix now: %s, sats=%u)\n",
                   (unsigned long)(g_gps.first_fix_ms / 1000), fix.valid ? "yes" : "lost again", g_gps.gnss.satellites());
        }
    }
}

// ---- 4. IMU: MPU6050 identity and readings --------------------------------------------------------------
void check_imu(Check& c) {
    i2c_master_bus_config_t bc = {};
    bc.i2c_port = IMU_I2C_PORT;
    bc.sda_io_num = static_cast<gpio_num_t>(IMU_SDA_GPIO);
    bc.scl_io_num = static_cast<gpio_num_t>(IMU_SCL_GPIO);
    bc.clk_source = I2C_CLK_SRC_DEFAULT;
    bc.glitch_ignore_cnt = 7;
    bc.flags.enable_internal_pullup = true;  // the GY-521 board has its own pull-ups too
    i2c_master_bus_handle_t bus = nullptr;
    esp_err_t e = i2c_new_master_bus(&bc, &bus);
    if (e != ESP_OK) {
        set(c, Result::Fail, "I2C bus setup failed on SDA=%d SCL=%d: %s", IMU_SDA_GPIO, IMU_SCL_GPIO,
            esp_err_to_name(e));
        return;
    }
    uint16_t addr = 0;
    static const uint16_t kAddrs[] = {0x68, 0x69};  // AD0 low / high
    for (uint16_t a : kAddrs) {
        if (i2c_master_probe(bus, a, 50) == ESP_OK) {
            addr = a;
            break;
        }
    }
    if (!addr) {
        i2c_del_master_bus(bus);
        set(c, Result::Fail, "nothing answers at 0x68/0x69 on SDA=GPIO%d SCL=GPIO%d (pins are PENDING)",
            IMU_SDA_GPIO, IMU_SCL_GPIO);
        return;
    }
    i2c_device_config_t dc = {};
    dc.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dc.device_address = addr;
    dc.scl_speed_hz = IMU_I2C_HZ;
    i2c_master_dev_handle_t dev = nullptr;
    e = i2c_master_bus_add_device(bus, &dc, &dev);

    uint8_t who = 0;
    const uint8_t reg_who = 0x75;
    if (e == ESP_OK) e = i2c_master_transmit_receive(dev, &reg_who, 1, &who, 1, 50);
    const uint8_t wake[] = {0x6B, 0x00};    // PWR_MGMT_1: leave sleep
    const uint8_t range[] = {0x1C, 0x00};   // ACCEL_CONFIG: +-2 g
    if (e == ESP_OK) e = i2c_master_transmit(dev, wake, sizeof wake, 50);
    if (e == ESP_OK) e = i2c_master_transmit(dev, range, sizeof range, 50);
    vTaskDelay(pdMS_TO_TICKS(100));

    double sum_g = 0, min_g = 99, max_g = 0;
    int samples = 0;
    const uint8_t reg_accel = 0x3B;
    for (int i = 0; i < 20 && e == ESP_OK; ++i) {
        uint8_t raw[6];
        e = i2c_master_transmit_receive(dev, &reg_accel, 1, raw, sizeof raw, 50);
        if (e != ESP_OK) break;
        const double ax = static_cast<int16_t>(raw[0] << 8 | raw[1]) / 16384.0;
        const double ay = static_cast<int16_t>(raw[2] << 8 | raw[3]) / 16384.0;
        const double az = static_cast<int16_t>(raw[4] << 8 | raw[5]) / 16384.0;
        const double g = std::sqrt(ax * ax + ay * ay + az * az);
        sum_g += g;
        min_g = g < min_g ? g : min_g;
        max_g = g > max_g ? g : max_g;
        ++samples;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    i2c_master_bus_rm_device(dev);
    i2c_del_master_bus(bus);

    if (e != ESP_OK) {
        set(c, Result::Fail, "found 0x%02X but I2C transfer failed: %s", addr, esp_err_to_name(e));
        return;
    }
    const double mean_g = sum_g / samples;
    // Boards sold as "MPU6050" often carry a register-compatible clone with another WHO_AM_I (ours reads 0x70).
    const char* name = nullptr;
    switch (who) {
        case 0x68: name = "MPU6050"; break;
        case 0x70: name = "MPU6500 (clone)"; break;
        case 0x71: name = "MPU9250 (clone)"; break;
        case 0x72: name = "MPU6050 clone (0x72)"; break;
        case 0x73: name = "MPU9255 (clone)"; break;
        case 0x98: name = "MPU6050 clone (0x98)"; break;
        default: break;
    }
    const bool gravity_ok = std::fabs(mean_g - 1.0) < 0.15;
    set(c, name && gravity_ok ? Result::Pass : Result::Fail,
        "addr=0x%02X WHO_AM_I=0x%02X %s samples=%d |a| mean=%.3f g min=%.3f max=%.3f (expect ~1.000 g at rest)",
        addr, who, name ? name : "(unknown chip, not an MPU6050)", samples, mean_g, min_g, max_g);
}

// ---- 5. Camera: OV3660 identity and one frame -----------------------------------------------------------
void check_camera(Check& c) {
    camera_config_t cfg = {};
    cfg.pin_pwdn = CAM_PWDN;
    cfg.pin_reset = CAM_RESET;
    cfg.pin_xclk = CAM_XCLK;
    cfg.pin_sccb_sda = CAM_SIOD;
    cfg.pin_sccb_scl = CAM_SIOC;
    cfg.pin_d7 = CAM_Y9;
    cfg.pin_d6 = CAM_Y8;
    cfg.pin_d5 = CAM_Y7;
    cfg.pin_d4 = CAM_Y6;
    cfg.pin_d3 = CAM_Y5;
    cfg.pin_d2 = CAM_Y4;
    cfg.pin_d1 = CAM_Y3;
    cfg.pin_d0 = CAM_Y2;
    cfg.pin_vsync = CAM_VSYNC;
    cfg.pin_href = CAM_HREF;
    cfg.pin_pclk = CAM_PCLK;
    cfg.xclk_freq_hz = 20000000;
    cfg.ledc_timer = LEDC_TIMER_0;
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.frame_size = FRAMESIZE_QVGA;
    cfg.jpeg_quality = 12;
    cfg.fb_count = 1;
    cfg.fb_location = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    cfg.sccb_i2c_port = CAM_SCCB_PORT;

    const esp_err_t e = esp_camera_init(&cfg);
    if (e != ESP_OK) {
        set(c, Result::Fail, "esp_camera_init failed: %s (ribbon cable seated? PSRAM on?)", esp_err_to_name(e));
        return;
    }
    sensor_t* s = esp_camera_sensor_get();
    const unsigned pid = s ? s->id.PID : 0;
    camera_fb_t* fb = nullptr;
    for (int i = 0; i < 3 && !fb; ++i) fb = esp_camera_fb_get();  // the first frame can time out
    size_t len = 0;
    unsigned w = 0, h = 0;
    bool jpeg = false;
    if (fb) {
        len = fb->len;
        w = fb->width;
        h = fb->height;
        // start-of-image marker at the front and end-of-image marker near the end (the driver can pad
        // after it). A frame shape check only, not a decode.
        bool soi = len > 4 && fb->buf[0] == 0xFF && fb->buf[1] == 0xD8, eoi = false;
        for (size_t i = len; soi && i >= 2 && len - i <= 62; --i)  // marker within the last 64 bytes
            if (fb->buf[i - 2] == 0xFF && fb->buf[i - 1] == 0xD9) { eoi = true; break; }
        jpeg = soi && eoi;
        esp_camera_fb_return(fb);
    }
    esp_camera_deinit();

    const bool ok = pid == OV3660_PID && jpeg && w == 320 && h == 240;
    set(c, ok ? Result::Pass : Result::Fail,
        "sensor PID=0x%04X (OV3660 = 0x%04X) frame=%s %ux%u (expect 320x240) %u bytes JPEG markers=%s "
        "(start+end marker only, not decoded)",
        pid, OV3660_PID, fb ? "captured" : "NONE", w, h, static_cast<unsigned>(len), jpeg ? "ok" : "BAD");
}

void blink_task(void* arg) {
    const bool all_passed = arg != nullptr;
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    const TickType_t half = pdMS_TO_TICKS(all_passed ? 1000 : 150);
    for (;;) {
        gpio_set_level(LED_GPIO, 0);
        vTaskDelay(half);
        gpio_set_level(LED_GPIO, 1);
        vTaskDelay(half);
    }
}

}  // namespace

extern "C" void app_main(void) {
    const esp_app_desc_t* app = esp_app_get_description();
    printf("\nSMOKE BEGIN ICVSP ECU smoke test, app %s built %s %s, ESP-IDF %s, %s build, reset reason: %s\n",
           app->version, app->date, app->time, esp_get_idf_version(), "BOARD",
           reset_reason_text(esp_reset_reason()));

    Check checks[] = {{"chip"}, {"signing"}, {"gps"}, {"imu"}, {"camera"}};
    check_chip(checks[0]);
    check_signing(checks[1]);
    check_gps(checks[2]);
    check_imu(checks[3]);
    check_camera(checks[4]);

    int pass = 0, fail = 0, skip = 0;
    for (const Check& c : checks) {
        printf("SMOKE %-8s %s %s\n", c.name, text(c.result), c.detail);
        (c.result == Result::Pass ? pass : c.result == Result::Fail ? fail : skip)++;
    }
    printf("SMOKE RESULT pass=%d fail=%d skip=%d (%s)\n", pass, fail, skip, "board");
    fflush(stdout);
    // LED: slow blink = nothing failed, fast = something failed
    if (xTaskCreate(blink_task, "led", 2048, fail == 0 ? reinterpret_cast<void*>(1) : nullptr, 1, nullptr) != pdPASS)
        printf("SMOKE warning: LED task could not start (out of memory)\n");
    if (g_gps.ready) watch_gps_forever();
    for (;;) vTaskDelay(portMAX_DELAY);
}
