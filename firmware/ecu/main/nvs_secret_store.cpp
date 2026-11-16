#include "nvs_secret_store.h"

#include "nvs.h"

namespace {
constexpr char kNamespace[] = "icvsp_id";
constexpr char kKey[] = "sign_seed";
}  // namespace

icvsp::SecretStore::Read NvsSecretStore::load_seed(uint8_t seed[32]) {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READONLY, &h) != ESP_OK) return Read::Absent;  // namespace not made yet
    size_t len = 32;
    const esp_err_t err = nvs_get_blob(h, kKey, seed, &len);
    nvs_close(h);
    if (err == ESP_ERR_NVS_NOT_FOUND) return Read::Absent;
    if (err != ESP_OK || len != 32) return Read::Corrupt;
    return Read::Ok;
}

bool NvsSecretStore::save_seed(const uint8_t seed[32]) {
    nvs_handle_t h;
    if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return false;
    const bool ok = nvs_set_blob(h, kKey, seed, 32) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}
