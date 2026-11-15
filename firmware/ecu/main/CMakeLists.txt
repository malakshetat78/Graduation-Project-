idf_component_register(
    SRCS "smoke_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES icvsp_core icvsp_board esp_driver_uart esp_driver_i2c esp_driver_gpio esp_psram esp_timer
             spi_flash esp_app_format)
