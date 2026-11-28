# T-012 camera: Sense camera settings, the JPEG check (plain C++, also unit tested on the laptop) and the
# bring-up test. Built into the camera firmware (CONFIG_ICVSP_APP_CAMERA, sdkconfig.camera).
idf_component_register(
    SRCS "sense_camera.cpp" "jpeg_check.cpp" "camera_test.cpp"
    INCLUDE_DIRS "."
    REQUIRES espressif__esp32-camera esp_psram esp_timer esp_app_format)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
