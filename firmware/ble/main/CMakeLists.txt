idf_component_register(SRCS "main.c" "icvsp_ble.c" "transport.c" "report_storage.c"
                       INCLUDE_DIRS "." REQUIRES bt nvs_flash esp_timer)
