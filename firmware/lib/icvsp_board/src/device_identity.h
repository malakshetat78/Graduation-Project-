#pragma once
// Each ECU's own signing key and device ID.
//
// The private seed is made on the chip at first boot from the hardware random number generator and kept in
// flash (NVS on the board), so every board has a different key, the key survives resets, and it never has to
// be typed, copied or committed anywhere. Only the public key leaves the chip (printed at boot, registered
// with the backend). Protecting the stored seed against someone reading the flash needs flash encryption +
// NVS encryption on the real board (not tested yet: see BRINGUP_CHECKLIST.md).
#include <cstddef>
#include <cstdint>

namespace icvsp {

// Where the seed is kept (NVS on the board, a fake in the tests).
class SecretStore {
public:
    enum class Read { Ok, Absent, Corrupt };
    virtual ~SecretStore() = default;
    virtual Read load_seed(uint8_t seed[32]) = 0;
    virtual bool save_seed(const uint8_t seed[32]) = 0;  // true once stored and committed
};

enum class KeyStatus {
    Loaded,       // the seed from an earlier boot
    Created,      // first boot: a new random seed, now stored
    Corrupt,      // a stored record of the wrong size: refused, never silently replaced (the identity would change)
    BadRandom,    // the random source returned all zeros or the same bytes twice: refused
    StoreFailed,  // could not store a new seed: refused (a key that is lost at reset would be useless)
};

const char* to_string(KeyStatus s);

// Loads this device's seed, or makes and stores one on first boot. fill_random fills a buffer with random
// bytes (esp_fill_random on the board). Returns Loaded or Created on success; seed is wiped on any failure.
KeyStatus load_or_create_seed(SecretStore& store, void (*fill_random)(void* buf, size_t len), uint8_t seed[32]);

// "ecu-" followed by the 12 hex digits of the chip's factory MAC address: unique per chip, stable across
// resets and reflashing, and needs no per-board configuration.
void device_id_from_mac(const uint8_t mac[6], char* out, size_t cap);

}  // namespace icvsp
