# ICVSP — Ed25519 Firmware (ESP32-S3)

ESP-IDF 5.5.5 project for Seeed XIAO ESP32-S3 Sense (8 MB flash).

## Modes

**Default (safe for development):** `CONFIG_ICVSP_ENABLE_SECURE_NVS=n`. Creates an Ed25519 key pair in RAM on each boot, signs a report, verifies the original signature and rejects altered data. No private key is stored in flash.

**Provisioned HMAC-backed encrypted storage (NOT hardware tested):** set `CONFIG_ICVSP_ENABLE_SECURE_NVS=y` and `CONFIG_ICVSP_HMAC_KEY_ID` to the actual verified, *already-provisioned* eFuse HMAC_UP key index on that board (0–5). The app calls `nvs_sec_provider_register_hmac`, then **only** `nvs_flash_read_security_cfg_v2` (no generation/burning), then `nvs_flash_secure_init_partition` for the dedicated `icvsp_keys` partition. On a truly empty encrypted partition it generates and saves a key pair; otherwise loads the stored pair, self-checks it and refuses to overwrite corrupt data. Failed initialization or storage means signing is disabled.

**Do not enable the provisioned mode or flash it until board key-block inspection, HMAC key provisioning plan, entropy review, partition migration/backups, and on-device tests are approved. eFuse operations are irreversible.** The repository contains no eFuse-burn commands or provisioning tools.

## Build
With ESP-IDF 5.5.5 shell:

```bat
idf.py set-target esp32s3
idf.py build
```

Flash size and a custom partition table are in `sdkconfig.defaults`. If using an existing project checkout with a previous `sdkconfig`, reconfigure it so the custom partition table gets applied. A partition-table change can invalidate old flash contents. Do not flash this firmware to any deployed device without migration review.

## Project source
- `main/main.c`: signing/verification smoke test.
- `main/key_storage.c`: load-or-create key logic, failure checks.
- `main/secure_storage.c`: optional HMAC-backed encrypted NVS adapter, strictly off by default.
- `main/Kconfig.projbuild`: explicit hardware-mode gate and eFuse key index.
- `partitions.csv`: factory application + dedicated `icvsp_keys` partition.
- `components/monocypher`: Ed25519 cryptography.

## Verification status
Previous RAM-only project and first fail-closed storage scaffold **built locally** on Windows. These latest encrypted-storage changes are **not yet compiled or tested** on the user's ESP-IDF setup, and are not hardware-verified. No persistence security claims until provisioning and reboot tests succeed.

## Hardware test plan
1. Inspect board eFuse key purposes and flash partition table.
2. Approve irreversible HMAC_UP key provisioning with secure handling and backup policy.
3. Build firmware with explicit hardware-mode settings.
4. First boot: encrypted partition initialized, new key pair saved; verify no plaintext secret in logs.
5. Power-cycle and verify public key continuity and signature verification.
6. Simulate invalid key slot, corrupted data, interrupted writes: device must fail closed.
7. Test backend registration, monotonic counters, and signed canonical hazard reports.

ESP-IDF reference: https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/api-reference/storage/nvs_encryption.html
