# This file supports the ESP-IDF framework only.
# Specifically, it targets the ESP32-C6 due to limited support in PlatformIO (see: https://github.com/platformio/platform-espressif32/issues/1225).
# Currently, we need to compile NimBLE CPP manually to enable BLE, and possibly WiFi, for ESP32-C6 targets.
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)
FILE(GLOB_RECURSE platform_sources ${CMAKE_SOURCE_DIR}/src/platform/**/*.*)
list(REMOVE_ITEM app_sources ${platform_sources})
FILE(GLOB_RECURSE esp32_sources ${CMAKE_SOURCE_DIR}/src/platform/esp32/*.*)
list(APPEND app_sources ${esp32_sources})
FILE(GLOB_RECURSE esp32_sources ${CMAKE_SOURCE_DIR}/src/platform/esp32/**/*.*)
list(APPEND app_sources ${esp32_sources})
list(REMOVE_ITEM app_sources ${CMAKE_SOURCE_DIR}/src/mesh/eth/ethClient.cpp)

# Register the component with ESP-IDF
idf_component_register(SRCS ${app_sources} INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/src)
