# Three firmwares from one project, picked in Kconfig (ICVSP_APP):
#   smoke test (default)  boot/sensor checks, smoke_main.cpp
#   report demo           signed report over the network to POST /reports (sdkconfig.report)
#   modem AT bridge       laptop types AT commands to a modem on UART2 (sdkconfig.modem_bridge)
if(CONFIG_ICVSP_APP_REPORT_DEMO)
    set(app_srcs "report_demo_main.cpp" "net_link.cpp" "http_transport.cpp" "nvs_secret_store.cpp")
elseif(CONFIG_ICVSP_APP_MODEM_BRIDGE)
    set(app_srcs "modem_bridge_main.cpp")
else()
    set(app_srcs "smoke_main.cpp")
endif()

# HTTPS test build: embed the test CA (made by results/bringup/tls/make_test_certs.py, git-ignored).
set(embed "")
if(CONFIG_ICVSP_TLS_TEST_CA)
    set(embed "${CMAKE_CURRENT_LIST_DIR}/../tls_test/ca.pem")
    if(NOT EXISTS "${embed}")
        message(FATAL_ERROR "CONFIG_ICVSP_TLS_TEST_CA is set but ${embed} is missing: run results/bringup/tls/make_test_certs.py")
    endif()
endif()

idf_component_register(
    SRCS ${app_srcs}
    EMBED_TXTFILES ${embed}
    INCLUDE_DIRS "." "../../lib/icvsp_sim/src"
    REQUIRES icvsp_core icvsp_board esp_driver_uart esp_driver_i2c esp_driver_gpio esp_psram esp_timer
             spi_flash esp_app_format esp_netif esp_event esp_wifi esp_eth esp_http_client nvs_flash mbedtls
             esp_driver_usb_serial_jtag)
