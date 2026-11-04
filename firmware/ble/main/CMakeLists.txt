idf_component_register(SRCS "main.c" "icvsp_ble.c" "transport.c"
                       INCLUDE_DIRS "." REQUIRES bt nvs_flash esp_timer)
