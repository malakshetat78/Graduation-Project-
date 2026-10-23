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

#endif
