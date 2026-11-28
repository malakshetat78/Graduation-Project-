#pragma once
// Camera settings for the XIAO ESP32-S3 Sense camera connector (Seeed pinout), so every program that starts the
// camera uses the same pins and clock. The pins are the same as the CAM_* values in main/board_pins.h.
#include "esp_camera.h"

namespace icvsp {

struct SenseCameraOptions {
    framesize_t frame_size = FRAMESIZE_QVGA;  // 320x240
    int jpeg_quality = 12;                    // 0-63, lower = better quality and bigger file
    size_t fb_count = 1;                      // frame buffers
    camera_fb_location_t fb_location = CAMERA_FB_IN_PSRAM;
    camera_grab_mode_t grab_mode = CAMERA_GRAB_LATEST;
};

// Sense pinout: XCLK 10, SIOD 40, SIOC 39, D7..D0 = 48 11 12 14 16 18 17 15, VSYNC 38, HREF 47, PCLK 13.
// No power-down or reset pin. 20 MHz XCLK, JPEG output. SCCB on I2C port 0 (CONFIG_SCCB_HARDWARE_I2C_PORT0 in
// sdkconfig.defaults), so port 1 stays free for the IMU.
camera_config_t sense_camera_config(const SenseCameraOptions& options);

// Takes one photo from the camera driver (esp_camera_fb_get). The driver sometimes leaves two pictures stuck
// together in one buffer (seen on the board at VGA and SVGA, 2026-11-24). Then the buffer is shortened to the first
// picture, which is complete on its own, and *cut is set to true. Give the frame back with esp_camera_fb_return
// as usual. Returns nullptr if the driver returned no frame.
camera_fb_t* get_photo(bool* cut);

}  // namespace icvsp
