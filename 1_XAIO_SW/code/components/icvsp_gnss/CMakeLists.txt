# T-014 L76K GNSS bring-up test (first version by Ahmed, Arduino). Built into the GNSS firmware
# (CONFIG_ICVSP_APP_GNSS, sdkconfig.gnss). Uses the shared NMEA parser from icvsp_board. gnss_uart.cpp is the GNSS
# reader of the all-in-one ECU firmware (CONFIG_ICVSP_APP_ECU, sdkconfig.ecu).
idf_component_register(
    SRCS "gnss_test.cpp" "gnss_uart.cpp"
    INCLUDE_DIRS "."
    REQUIRES icvsp_board esp_driver_uart esp_driver_usb_serial_jtag esp_timer vfs)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
