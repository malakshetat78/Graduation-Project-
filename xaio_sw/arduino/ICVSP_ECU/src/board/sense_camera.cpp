#include "sense_camera.h"

namespace icvsp {

camera_config_t sense_camera_config(const SenseCameraOptions& options)
{
    camera_config_t config{};
    config.pin_pwdn = -1;
    config.pin_reset = -1;
    config.pin_xclk = 10;
    config.pin_sccb_sda = 40;
    config.pin_sccb_scl = 39;
    config.pin_d7 = 48;
    config.pin_d6 = 11;
    config.pin_d5 = 12;
    config.pin_d4 = 14;
    config.pin_d3 = 16;
    config.pin_d2 = 18;
    config.pin_d1 = 17;
    config.pin_d0 = 15;
    config.pin_vsync = 38;
    config.pin_href = 47;
    config.pin_pclk = 13;
    config.xclk_freq_hz = 20000000;
    config.ledc_timer = LEDC_TIMER_0;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = options.frame_size;
    config.jpeg_quality = options.jpeg_quality;
    config.fb_count = options.fb_count;
    config.fb_location = options.fb_location;
    config.grab_mode = options.grab_mode;
    // SCCB port: the Arduino core builds the camera driver for I2C port 1 (CONFIG_SCCB_HARDWARE_I2C_PORT1),
    // so keep the IMU on Wire (port 0).
    return config;
}

}  // namespace icvsp
