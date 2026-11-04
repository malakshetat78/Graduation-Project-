#pragma once
#include "esp_err.h"
#include "transport.h"

/* Dedicated report_q NVS partition. Initialize before starting BLE. */
esp_err_t report_storage_open(transport_t *transport);
/* Durable before ESP_OK: appends one unacknowledged report by its transport ID. */
esp_err_t report_storage_append(uint16_t id, const uint8_t *bytes, size_t len);
/* Persist removal BEFORE accepting the matching BLE ACK. */
esp_err_t report_storage_remove(uint16_t id);
/* For orderly shutdown and host-test simulated power cycles. */
void report_storage_close(void);
