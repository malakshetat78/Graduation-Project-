# Several firmwares from one project, picked in Kconfig (ICVSP_APP):
#   smoke test (default)  boot/sensor checks, smoke_main.cpp
#   report demo           signed report over the network to POST /reports (sdkconfig.report)
#   camera test           T-012 camera bring-up, components/icvsp_camera (sdkconfig.camera)
#   IMU test              T-010 MPU6050 bring-up, components/icvsp_imu (sdkconfig.imu)
#   GNSS test             T-014 L76K bring-up, components/icvsp_gnss (sdkconfig.gnss)
#   BLE test              T-016 BLE GATT bring-up, components/icvsp_ble (sdkconfig.ble)
#   all-in-one ECU        every part together: IMU, GNSS, camera, BLE, Wi-Fi (sdkconfig.ecu)
if(CONFIG_ICVSP_APP_REPORT_DEMO)
    set(app_srcs "report_demo_main.cpp" "net_link.cpp" "http_transport.cpp" "nvs_secret_store.cpp")
elseif(CONFIG_ICVSP_APP_CAMERA)
    set(app_srcs "camera_main.cpp")
elseif(CONFIG_ICVSP_APP_IMU)
    set(app_srcs "imu_main.cpp")
elseif(CONFIG_ICVSP_APP_GNSS)
    set(app_srcs "gnss_main.cpp")
elseif(CONFIG_ICVSP_APP_ECU)
    set(app_srcs "ecu_main.cpp" "net_link.cpp" "http_transport.cpp" "nvs_secret_store.cpp")
elseif(CONFIG_ICVSP_APP_BLE)
    set(app_srcs "ble_main.cpp" "nvs_secret_store.cpp")
else()
    set(app_srcs "smoke_main.cpp")
endif()

idf_component_register(
    SRCS ${app_srcs}
    INCLUDE_DIRS "."
    REQUIRES icvsp_core icvsp_board icvsp_camera icvsp_imu icvsp_gnss icvsp_ble esp_driver_uart esp_driver_i2c esp_driver_gpio esp_psram esp_timer
             spi_flash esp_app_format esp_netif esp_event esp_wifi esp_http_client nvs_flash mbedtls)

if(CONFIG_ICVSP_APP_BLE)
    # Test clock for the BLE test reports (no GNSS in that build): UTC seconds when CMake configured the project.
    string(TIMESTAMP icvsp_build_utc_s "%s" UTC)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ICVSP_BUILD_UTC_S=${icvsp_build_utc_s}LL)
endif()
