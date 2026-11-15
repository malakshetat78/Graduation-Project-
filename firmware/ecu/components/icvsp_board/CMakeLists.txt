# Hardware-free board logic from ../lib/icvsp_board (NMEA parser, report queue, uplink, Wi-Fi state, app),
# unchanged. The same sources are unit-tested on the laptop with `pio test -e native`.
idf_component_register(
    SRC_DIRS "../../../lib/icvsp_board/src"
    INCLUDE_DIRS "../../../lib/icvsp_board/src"
    REQUIRES icvsp_core)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
