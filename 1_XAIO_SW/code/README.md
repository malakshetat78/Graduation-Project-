# 1_XAIO_SW/code

The ECU firmware: one ESP-IDF v5.5 project for the XIAO ESP32-S3 Sense. Open **this folder** in VS Code.

```bash
python build.py prod flash monitor -p <PORT>   # in an ESP-IDF terminal; <PORT>: COM5 / /dev/cu.usbmodem1101 / /dev/ttyACM0
```

Build types, flashing, bring-up and running in the car: [../doc/BUILD_AND_FLASH.md](../doc/BUILD_AND_FLASH.md).
Full reference (wiring, each test firmware, common errors): [../doc/README.md](../doc/README.md).
