#pragma once
// icvsp::SecretStore on NVS (namespace "icvsp_id", key "sign_seed", a 32-byte blob). The partition is plain
// flash unless flash encryption and NVS encryption are enabled on the board (not done yet).
#include "../board/device_identity.h"

class NvsSecretStore : public icvsp::SecretStore {
public:
    Read load_seed(uint8_t seed[32]) override;
    bool save_seed(const uint8_t seed[32]) override;
};
