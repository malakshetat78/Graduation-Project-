#ifndef ICVSP_KEY_STORAGE_H
#define ICVSP_KEY_STORAGE_H

#include <stdint.h>
#include <stdbool.h>

#define ICVSP_SECRET_KEY_SIZE 64
#define ICVSP_PUBLIC_KEY_SIZE 32

typedef struct {
    uint8_t secret_key[ICVSP_SECRET_KEY_SIZE];
    uint8_t public_key[ICVSP_PUBLIC_KEY_SIZE];
} icvsp_key_pair_t;

bool icvsp_load_or_create_keys(icvsp_key_pair_t *keys);

#endif