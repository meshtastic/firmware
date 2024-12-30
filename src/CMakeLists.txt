FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)
FILE(GLOB_RECURSE platform_sources ${CMAKE_SOURCE_DIR}/src/platform/**/*.*)
list(REMOVE_ITEM app_sources ${platform_sources})
FILE(GLOB_RECURSE esp32_sources ${CMAKE_SOURCE_DIR}/src/platform/esp32/*.*)
list(APPEND app_sources ${esp32_sources})
FILE(GLOB_RECURSE esp32_sources ${CMAKE_SOURCE_DIR}/src/platform/esp32/**/*.*)
list(APPEND app_sources ${esp32_sources})

# Register the component with ESP-IDF
idf_component_register(SRCS ${app_sources} INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/src)
