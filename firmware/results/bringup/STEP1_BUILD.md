# Step 1 - build the current firmware as-is (2026-11-15)

Level of evidence: **cross-compiled only**. Nothing here was flashed, run in QEMU, or run on a board.

## Source

- Branch `fix/ble-preserve-unacked-reports` (Malak), commit `a452c6647acee3ecccd1248158f1f1034e29a8d1`
  (2026-11-05). `firmware/ble` exists only on that branch; `espidf_security` and `espdet_qemu` are also on `main`.
- Exported into an empty folder, so no source file was changed:

```bash
git fetch origin fix/ble-preserve-unacked-reports
mkdir step1 && git archive a452c6647acee3ecccd1248158f1f1034e29a8d1 firmware | tar -x -C step1
```

None of the three projects pins managed components except `espdet_qemu` (`espressif/esp-dl ^3.1.3`), so the
ESP-IDF version below fixes everything the two built projects use.

## Toolchain

| Tool | Version |
|---|---|
| ESP-IDF | v5.5.5 (`~/esp/esp-idf`) |
| Compiler | xtensa-esp-elf-gcc 14.2.0 (crosstool-NG esp-14.2.0_20260121) |
| CMake | 4.4.3 |
| Python | 3.13.13 (ESP-IDF venv) |
| Host | macOS 26.6.2, arm64 |

Setup once: clone ESP-IDF v5.5.5 to `~/esp/esp-idf` and run `./install.sh esp32s3` there. In every new shell:

```bash
. ~/esp/esp-idf/export.sh
```

## Commands and results

In each project folder (`step1/firmware/<project>`):

```bash
idf.py set-target esp32s3 build
```

| Project | Exit | Result | Image |
|---|---|---|---|
| `espidf_security` (Ed25519 signing **demo**) | 0 | Built, 1 warning: `secure_storage.c:21` unused variable `initialized` | `icvsp_ed25519.bin`, 226,432 B, 78% of the 1 MB app partition free |
| `ble` (NimBLE + NVS report queue) | 2 | **Failed as-is.** The partition table ends at `0x230000` (2.19 MiB: 2 MiB app at `0x10000` + 128 KB `report_q`), but `sdkconfig.defaults` does not set the flash size, so ESP-IDF assumes 2 MB (`build_ble.log`: "occupies 2.2MB of flash (2293760 bytes) which does not fit in configured flash size 2MB") | - |
| `ble` with 8 MB flash (override below) | 0 | Built, 0 warnings | `icvsp_xiao_ble.bin`, 487,520 B, 77% of the 2 MB app partition free |
| `espdet_qemu` (camera AI detector) | 2 | Not built: CMake stops because the model `ai/models/espdet_pico_224_esp32s3.espdl` is not in git (the samples in `main/samples/` are missing too). **Out of scope** for bring-up, see below. | - |

What `espidf_security` is: a demo of the signing and the backend contract. Its secure storage is off by default
(`CONFIG_ICVSP_ENABLE_SECURE_NVS=n`, "Secure NVS disabled by default. No persistence.") and it refuses to sign
live hazards without a stored boot ID. Building it proves the signing code compiles for the chip, not that keys
or boot IDs persist.

### The `ble` flash-size override (no project file edited)

The XIAO ESP32-S3 Sense has 8 MB flash. Create `flash8mb.defaults` next to the project folder:

```bash
printf 'CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y\nCONFIG_ESPTOOLPY_FLASHSIZE="8MB"\n' > ../flash8mb.defaults
rm -rf build sdkconfig
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;$PWD/../flash8mb.defaults" set-target esp32s3 build
```

The proper fix is the same two lines in `firmware/ble/sdkconfig.defaults` on Malak's branch (sent to her; not
changed here because it is her unmerged branch). The new `firmware/ecu` project sets 8 MB in its own config.

### Image hashes

```bash
shasum -a 256 build/icvsp_xiao_ble.bin build/icvsp_ed25519.bin
```

```
5a6385607f3ce70905e03d403c65dad31e56441888a2271a25ffde556bedcae8  icvsp_xiao_ble.bin
bc1153d3d8a53f43dd598b49bfef9404ed27f4a194e35e647af150df8a067fda  icvsp_ed25519.bin
```

## Flashing (documented, NOT run yet - board not delivered)

The XIAO ESP32-S3 has native USB; `ble` uses the USB-Serial/JTAG console. Find the port, then flash one port:

```bash
ls /dev/cu.usbmodem*
idf.py -p /dev/cu.usbmodem101 flash monitor
```

(`/dev/cu.usbmodem101` is an example; use the name `ls` printed.) The equivalent esptool command, printed by
the build: `python -m esptool --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash
--flash_mode dio --flash_size 8MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x8000
build/partition_table/partition-table.bin 0x10000 build/icvsp_xiao_ble.bin`.

If the port does not appear: hold BOOT, tap RESET, release BOOT, then flash again. Fit the Wi-Fi/BT antenna before
any radio test (Seeed: without it the board may fail to connect).

## Logs

`step1_build_logs/` - full `idf.py` output for every build above (scratch paths shortened to `<scratch>`, home
folder to `~`), `toolchain.txt`, and `flash8mb.defaults`.

## Open items

1. Malak: add the two 8 MB lines to `ble/sdkconfig.defaults`; fix or drop the unused `initialized` in
   `espidf_security/main/secure_storage.c`.
2. `espdet_qemu` (camera AI detector) is out of scope for the bring-up task: the detector model is not ready,
   and the ECU's hazard detection and the boot/sensor smoke test do not depend on it. Recorded here only so the
   failed build is not hidden.
