# T-010 MPU6050 IMU bring-up test (first version by Rana, Arduino). Built into the IMU firmware
# (CONFIG_ICVSP_APP_IMU, sdkconfig.imu). mpu_imu.cpp is the IMU driver of the all-in-one ECU firmware
# (CONFIG_ICVSP_APP_ECU, sdkconfig.ecu).
idf_component_register(
    SRCS "imu_test.cpp" "accel_cal.cpp" "mpu_imu.cpp"
    INCLUDE_DIRS "."
    REQUIRES icvsp_board esp_driver_i2c esp_driver_usb_serial_jtag esp_timer nvs_flash)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
