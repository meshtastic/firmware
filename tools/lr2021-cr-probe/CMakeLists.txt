cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(lr2021_cr_probe)

target_sources(app PRIVATE src/main.c src/lr2021_probe.c)
target_include_directories(app PRIVATE src)

# Build the TX role with: west build -b <board> . -- -DROLE=TX
if(ROLE STREQUAL "TX")
  target_compile_definitions(app PRIVATE PROBE_ROLE_TX=1)
endif()
