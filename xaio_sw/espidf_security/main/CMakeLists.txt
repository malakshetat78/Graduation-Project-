idf_component_register(
    SRCS "main.c" "key_storage.c" "secure_storage.c" "report_signer.c" "replay_identity.c" "replay_identity_esp.c"
    INCLUDE_DIRS "."
    REQUIRES monocypher esp_hw_support nvs_flash nvs_sec_provider
)
