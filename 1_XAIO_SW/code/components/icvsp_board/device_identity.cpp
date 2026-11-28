#include "device_identity.h"

#include <cstdio>
#include <cstring>

namespace icvsp {

namespace {

void wipe(uint8_t* p, size_t n) {
    volatile uint8_t* v = p;
    while (n--) *v++ = 0;
}

bool all_zero(const uint8_t* p, size_t n) {
    uint8_t acc = 0;
    for (size_t i = 0; i < n; ++i) acc |= p[i];
    return acc == 0;
}

}  // namespace

const char* to_string(KeyStatus s) {
    switch (s) {
        case KeyStatus::Loaded: return "loaded";
        case KeyStatus::Created: return "created";
        case KeyStatus::Corrupt: return "corrupt";
        case KeyStatus::BadRandom: return "bad-random";
        case KeyStatus::StoreFailed: return "store-failed";
    }
    return "?";
}

KeyStatus load_or_create_seed(SecretStore& store, void (*fill_random)(void* buf, size_t len), uint8_t seed[32]) {
    switch (store.load_seed(seed)) {
        case SecretStore::Read::Ok:
            if (!all_zero(seed, 32)) return KeyStatus::Loaded;
            wipe(seed, 32);
            return KeyStatus::Corrupt;
        case SecretStore::Read::Corrupt:
            wipe(seed, 32);
            return KeyStatus::Corrupt;
        case SecretStore::Read::Absent:
            break;
    }
    // Two draws must differ: a stuck or missing random source must never become a key.
    uint8_t check[32];
    fill_random(seed, 32);
    fill_random(check, 32);
    const bool same = std::memcmp(seed, check, 32) == 0;
    wipe(check, 32);
    if (all_zero(seed, 32) || same) {
        wipe(seed, 32);
        return KeyStatus::BadRandom;
    }
    if (!store.save_seed(seed)) {
        wipe(seed, 32);
        return KeyStatus::StoreFailed;
    }
    return KeyStatus::Created;
}

void device_id_from_mac(const uint8_t mac[6], char* out, size_t cap) {
    std::snprintf(out, cap, "ecu-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

}  // namespace icvsp
