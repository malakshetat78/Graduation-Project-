#include "report_storage.h"
#include <stdio.h>
#include <string.h>
#include "nvs.h"
#include "nvs_flash.h"

#define PARTITION_NAME "report_q"
#define STORAGE_NS "pending"
#define HEADER_SIZE 4

static nvs_handle_t handle;
static bool opened;

static void key_for(unsigned slot, char key[8]) {
    (void)snprintf(key, 8, "r%02u", slot);
}

static esp_err_t read_slot(unsigned slot, uint8_t *buffer, size_t *length) {
    char key[8];
    key_for(slot, key);
    return nvs_get_blob(handle, key, buffer, length);
}

esp_err_t report_storage_open(transport_t *t) {
    if (opened) return ESP_ERR_INVALID_STATE;
    esp_err_t e = nvs_flash_init_partition(PARTITION_NAME);
    if (e != ESP_OK) return e;  /* Never erase persistent reports automatically. */
    e = nvs_open_from_partition(PARTITION_NAME, STORAGE_NS, NVS_READWRITE, &handle);
    if (e != ESP_OK) return e;
    opened = true;

    /* Reconstruct strictly in transport-ID order, not NVS slot order. */
    uint8_t buffer[REPORT_MAX + HEADER_SIZE];
    uint16_t max_id = 0;
    for (unsigned slot = 0; slot < REPORT_QUEUE; ++slot) {
        size_t length = sizeof(buffer);
        e = read_slot(slot, buffer, &length);
        if (e == ESP_ERR_NVS_NOT_FOUND) continue;
        if (e != ESP_OK) return e;
        if (length < HEADER_SIZE || length > sizeof(buffer)) return ESP_ERR_INVALID_SIZE;
        uint16_t id = ((uint16_t)buffer[0] << 8) | buffer[1];
        uint16_t bytes_len = ((uint16_t)buffer[2] << 8) | buffer[3];
        if (!id || !bytes_len || bytes_len > REPORT_MAX ||
            length != HEADER_SIZE + bytes_len || t->count >= REPORT_QUEUE)
            return ESP_ERR_INVALID_SIZE;

        unsigned insertion = t->count;
        for (unsigned i = 0; i < t->count; ++i) {
            if (t->items[i].id == id) return ESP_ERR_INVALID_STATE;
            if (t->items[i].id > id) { insertion = i; break; }
        }
        memmove(&t->items[insertion + 1], &t->items[insertion],
                (t->count - insertion) * sizeof(report_t));
        report_t *r = &t->items[insertion];
        r->id = id;
        r->len = bytes_len;
        memcpy(r->data, buffer + HEADER_SIZE, bytes_len);
        ++t->count;
        if (id > max_id) max_id = id;
    }
    t->next_id = (uint32_t)max_id + 1;
    return ESP_OK;
}

esp_err_t report_storage_append(uint16_t id, const uint8_t *bytes, size_t len) {
    if (!opened || !id || !bytes || !len || len > REPORT_MAX)
        return ESP_ERR_INVALID_ARG;
    unsigned vacant = REPORT_QUEUE;
    for (unsigned i = 0; i < REPORT_QUEUE; ++i) {
        size_t existing_length = 0;
        esp_err_t e = read_slot(i, NULL, &existing_length);
        if (e == ESP_ERR_NVS_NOT_FOUND) { vacant = i; break; }
        if (e != ESP_OK) return e;
    }
    if (vacant == REPORT_QUEUE) return ESP_ERR_NO_MEM;
    uint8_t record[HEADER_SIZE + REPORT_MAX];
    record[0] = id >> 8; record[1] = id & 255;
    record[2] = (uint16_t)len >> 8; record[3] = (uint16_t)len & 255;
    memcpy(record + HEADER_SIZE, bytes, len);
    char key[8];
    key_for(vacant, key);
    esp_err_t e = nvs_set_blob(handle, key, record, HEADER_SIZE + len);
    if (e != ESP_OK) return e;
    return nvs_commit(handle);
}

esp_err_t report_storage_remove(uint16_t id) {
    if (!opened || !id) return ESP_ERR_INVALID_ARG;
    uint8_t buffer[HEADER_SIZE + REPORT_MAX];
    for (unsigned i = 0; i < REPORT_QUEUE; ++i) {
        size_t length = sizeof(buffer);
        esp_err_t e = read_slot(i, buffer, &length);
        if (e == ESP_ERR_NVS_NOT_FOUND) continue;
        if (e != ESP_OK) return e;
        if (length < HEADER_SIZE) return ESP_ERR_INVALID_SIZE;
        uint16_t stored_id = ((uint16_t)buffer[0] << 8) | buffer[1];
        if (stored_id != id) continue;
        char key[8];
        key_for(i, key);
        e = nvs_erase_key(handle, key);
        if (e != ESP_OK) return e;
        return nvs_commit(handle);
    }
    return ESP_ERR_NOT_FOUND;
}

void report_storage_close(void) {
    if (!opened) return;
    nvs_close(handle);
    opened = false;
}
