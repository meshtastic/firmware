cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
add_compile_definitions(ARDUINO_ARCH_ESP32=1)
project(firmware)
