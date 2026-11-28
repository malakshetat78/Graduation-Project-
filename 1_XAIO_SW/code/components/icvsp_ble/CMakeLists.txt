# T-016 BLE bring-up test (first version by Mariam, NimBLE-Arduino). Built into the BLE firmware
# (CONFIG_ICVSP_APP_BLE, sdkconfig.ble). Uses the ESP-IDF NimBLE host. Compiled only when Bluetooth is on, so the
# other firmwares (Bluetooth off) do not need it.
set(srcs "")
if(CONFIG_BT_NIMBLE_ENABLED)
    set(srcs "ble_test.cpp")
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES bt nvs_flash esp_timer)
if(srcs)
    # NimBLE tables use designated initializers; the members left out are meant to be zero.
    target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wno-missing-field-initializers)
endif()
