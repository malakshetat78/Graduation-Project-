#pragma once
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
/* Call after icvsp_ble_start, from a task (not ISR).
 * Copies opaque signed wire bytes into NVS before returning ESP_OK.
 * ESP_ERR_TIMEOUT means the durable queue is full; the producer must retain/retry.
 * Other errors indicate storage failure; do not discard the producer's copy.
 * No signing, JSON transformation, or signature validation is done here.
 */
esp_err_t icvsp_ble_start(void);
esp_err_t icvsp_ble_enqueue(const uint8_t *signed_bytes, size_t length);
