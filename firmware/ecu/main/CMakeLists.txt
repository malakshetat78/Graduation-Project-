# Two firmwares from one project, picked in Kconfig (ICVSP_APP):
#   smoke test (default)  boot/sensor checks, smoke_main.cpp
#   report demo           signed report over the network to POST /reports (sdkconfig.report)
if(CONFIG_ICVSP_APP_REPORT_DEMO)
    set(app_srcs "report_demo_main.cpp" "net_link.cpp" "http_transport.cpp" "nvs_secret_store.cpp")
else()
    set(app_srcs "smoke_main.cpp")
endif()

idf_component_register(
    SRCS ${app_srcs}
    INCLUDE_DIRS "." "../../lib/icvsp_sim/src"
    REQUIRES icvsp_core icvsp_board esp_driver_uart esp_driver_i2c esp_driver_gpio esp_psram esp_timer
             spi_flash esp_app_format esp_netif esp_event esp_wifi esp_eth esp_http_client nvs_flash mbedtls)
