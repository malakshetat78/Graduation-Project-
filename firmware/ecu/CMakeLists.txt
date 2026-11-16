# ICVSP ECU firmware - ESP-IDF 5.5.5, target esp32s3 (Seeed XIAO ESP32-S3 Sense).
# Currently the boot/sensor smoke test (main/smoke_main.cpp). Reuses ../lib unchanged through the
# components in components/.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(icvsp_ecu)
