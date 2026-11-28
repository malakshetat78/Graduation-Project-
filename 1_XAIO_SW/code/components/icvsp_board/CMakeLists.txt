# Board logic with no hardware calls: NMEA (GNSS) parser, report queue, uplink, Wi-Fi state, ECU app, device ID.
idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "."
    REQUIRES icvsp_core)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
