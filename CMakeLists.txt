# This file supports the ESP-IDF framework only.
# Specifically, it targets the ESP32-C6 due to limited support in PlatformIO (see: https://github.com/platformio/platform-espressif32/issues/1225).
# Currently, we need to compile NimBLE CPP manually to enable BLE, and possibly WiFi, for ESP32-C6 targets.
cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(firmware)
