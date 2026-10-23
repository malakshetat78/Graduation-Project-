#include "secure_storage.h"

#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "monocypher.h"

#if CONFIG_ICVSP_ENABLE_SECURE_NVS
#include "nvs.h"
#include "nvs_flash.h"
#include "nvs_sec_provider.h"
#include "esp_err.h"
#endif

#define STORAGE_PARTITION "icvsp_keys"
#define STORAGE_NAMESPACE "device_keys"
#define STORAGE_ITEM "ed25519"
#define KEY_PAIR_LENGTH 96

static bool initialized = false;

bool secure_storage_init(void)
{
#if CONFIG_ICVSP_ENABLE_SECURE_NVS
    if (initialized) return true;
    if (CONFIG_ICVSP_HMAC_KEY_ID < 0 || CONFIG_ICVSP_HMAC_KEY_ID > 5) {
        puts("ICVSP: HMAC eFuse key index not set. Storage disabled.");
        return false;
    }

    nvs_sec_config_hmac_t scheme_cfg = {
        .hmac_key_id = (hmac_key_id_t)CONFIG_ICVSP_HMAC_KEY_ID
    };
    nvs_sec_scheme_t *scheme = NULL;
    nvs_sec_cfg_t cfg = {0};

    esp_err_t err = nvs_sec_provider_register_hmac(&scheme_cfg, &scheme);
    if (err != ESP_OK) {
        puts("ICVSP: HMAC scheme setup failed.");
        return false;
    }

    // IMPORTANT: read-only derivation: never generate or burn eFuse keys.
    err = nvs_flash_read_security_cfg_v2(scheme, &cfg);
    if (err == ESP_OK) {
        err = nvs_flash_secure_init_partition(STORAGE_PARTITION, &cfg);
    }
    crypto_wipe(&cfg, sizeof(cfg));
    esp_err_t cleanup = nvs_sec_provider_deregister(scheme);
    if (err != ESP_OK || cleanup != ESP_OK) {
        printf("ICVSP: Secure NVS unavailable (error %d).\n", (int)err);
        return false;
    }
    initialized = true;
    return true;
#else
    puts("ICVSP: Secure NVS disabled by default. No persistence.");
    return false;
#endif
}

bool secure_storage_save_key(const uint8_t *key, size_t length)
{
#if CONFIG_ICVSP_ENABLE_SECURE_NVS
    if (!initialized || key == NULL || length != KEY_PAIR_LENGTH) return false;
    nvs_handle_t handle;
    if (nvs_open_from_partition(STORAGE_PARTITION, STORAGE_NAMESPACE, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(handle, STORAGE_ITEM, key, length);
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
#else
    (void)key; (void)length;
    return false;
#endif
}

icvsp_store_result_t secure_storage_load_key(uint8_t *key, size_t length)
{
#if CONFIG_ICVSP_ENABLE_SECURE_NVS
    if (!initialized || key == NULL || length != KEY_PAIR_LENGTH) return ICVSP_STORE_ERROR;
    nvs_handle_t handle;
    esp_err_t err = nvs_open_from_partition(STORAGE_PARTITION, STORAGE_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ICVSP_STORE_NOT_FOUND;
    if (err != ESP_OK) return ICVSP_STORE_ERROR;
    size_t actual = length;
    err = nvs_get_blob(handle, STORAGE_ITEM, key, &actual);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ICVSP_STORE_NOT_FOUND;
    if (err != ESP_OK || actual != length) {
        crypto_wipe(key, length);
        return ICVSP_STORE_ERROR;
    }
    return ICVSP_STORE_OK;
#else
    (void)key; (void)length;
    return ICVSP_STORE_ERROR;
#endif
}
