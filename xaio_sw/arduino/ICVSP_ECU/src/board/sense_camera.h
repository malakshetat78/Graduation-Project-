#pragma once
// Camera settings for the XIAO ESP32-S3 Sense camera connector (Seeed pinout), shared by the smoke test
// and the camera bring-up test so both always use the same pins and clock.
#include "esp_camera.h"

namespace icvsp {

struct SenseCameraOptions {
    framesize_t frame_size = FRAMESIZE_QVGA;      // 320x240
    int jpeg_quality = 12;                        // 0-63, lower = better quality and bigger file
    size_t fb_count = 1;                          // frame buffers
    camera_fb_location_t fb_location = CAMERA_FB_IN_PSRAM;
    camera_grab_mode_t grab_mode = CAMERA_GRAB_LATEST;
};

// Sense pinout: XCLK 10, SIOD 40, SIOC 39, D7..D0 = 48 11 12 14 16 18 17 15, VSYNC 38, HREF 47, PCLK 13.
// No power-down or reset pin. 20 MHz XCLK, JPEG output, SCCB on I2C port 1 (set by the Arduino core).
camera_config_t sense_camera_config(const SenseCameraOptions& options);

}  // namespace icvsp
