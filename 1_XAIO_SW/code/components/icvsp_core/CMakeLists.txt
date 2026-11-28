# Device logic with no hardware calls: hazard detection, fusion, events, Ed25519 signing.
idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "."
    REQUIRES icvsp_monocypher)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
