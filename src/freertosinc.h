#pragma once

// The FreeRTOS includes are in a different directory on ESP32 and I can't figure out how to make that work with platformio gcc
// options so this is my quick hack to make things work

#ifdef ARDUINO_ARCH_ESP32
#define HAS_FREE_RTOS
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#else
// not yet supported on cubecell
#ifndef CubeCell_BoardPlus
#define HAS_FREE_RTOS
#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <task.h>
#else

#include <Arduino.h>

typedef uint32_t TickType_t;
typedef uint32_t BaseType_t;

#define portMAX_DELAY UINT32_MAX

#endif
#endif