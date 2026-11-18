# Hardware-free device logic from ../lib/icvsp_core (detection, fusion, events, signing), unchanged.
# The same sources are unit-tested on the laptop with `pio test -e native`.
idf_component_register(
    SRC_DIRS "../../../lib/icvsp_core/src"
    INCLUDE_DIRS "../../../lib/icvsp_core/src"
    REQUIRES icvsp_monocypher)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
