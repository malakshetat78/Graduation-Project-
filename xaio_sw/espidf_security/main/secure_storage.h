#ifndef ICVSP_SECURE_STORAGE_H
#define ICVSP_SECURE_STORAGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    ICVSP_STORE_OK = 0,
    ICVSP_STORE_NOT_FOUND,
    ICVSP_STORE_ERROR
} icvsp_store_result_t;

bool secure_storage_init(void);
bool secure_storage_save_key(const uint8_t *key, size_t length);
icvsp_store_result_t secure_storage_load_key(uint8_t *key, size_t length);

/* Reserve a strictly increasing boot ID on encrypted NVS.
 * found=false is allowed ONLY for the first verified-empty initialization.
 * Failure or corruption must prevent reports from being signed.
 */
bool secure_storage_read_boot_counter(uint32_t *previous, bool *found);
bool secure_storage_write_boot_counter(uint32_t new_boot);

#endif
