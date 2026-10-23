#include "key_storage.h"

#include <stdio.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_random.h"
#include "monocypher.h"
#include "monocypher-ed25519.h"
#include "secure_storage.h"

bool icvsp_load_or_create_keys(icvsp_key_pair_t *keys)
{
    if (keys == NULL) return false;
    memset(keys, 0, sizeof(*keys));

#if CONFIG_ICVSP_ENABLE_SECURE_NVS
    if (!secure_storage_init()) {
        puts("ICVSP: Secure storage unavailable; refusing key generation.");
        return false;
    }

    icvsp_store_result_t status = secure_storage_load_key((uint8_t *)keys, sizeof(*keys));
    if (status == ICVSP_STORE_OK) {
        static const uint8_t probe[] = "ICVSP key-pair self-test";
        uint8_t signature[64];
        crypto_ed25519_sign(signature, keys->secret_key, probe, sizeof(probe) - 1);
        int result = crypto_ed25519_check(signature, keys->public_key, probe, sizeof(probe) - 1);
        crypto_wipe(signature, sizeof(signature));
        if (result != 0) {
            crypto_wipe(keys, sizeof(*keys));
            puts("ICVSP: Stored keys invalid. Refusing to replace them.");
            return false;
        }
        puts("ICVSP: Loaded Ed25519 key pair from encrypted NVS.");
        return true;
    }
    if (status != ICVSP_STORE_NOT_FOUND) {
        puts("ICVSP: Storage error; refusing to overwrite existing keys.");
        return false;
    }
#else
    // DEVELOPMENT ONLY: RAM-only key rotates on every boot.
#endif

    uint8_t seed[32] = {0};
    // ESP32 hardware RNG requires its documented entropy prerequisites.
    // Verify them on physical hardware before production provisioning.
    esp_fill_random(seed, sizeof(seed));
    crypto_ed25519_key_pair(keys->secret_key, keys->public_key, seed);
    crypto_wipe(seed, sizeof(seed));

#if CONFIG_ICVSP_ENABLE_SECURE_NVS
    if (!secure_storage_save_key((const uint8_t *)keys, sizeof(*keys))) {
        crypto_wipe(keys, sizeof(*keys));
        puts("ICVSP: Failed to persist keys securely; refusing to proceed.");
        return false;
    }
    puts("ICVSP: New key pair persisted in encrypted NVS.");
#else
    puts("ICVSP: DEVELOPMENT MODE - RAM-only key generated, not persisted.");
#endif
    return true;
}
