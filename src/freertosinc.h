#pragma once

// The FreeRTOS includes are in a different directory on ESP32 and I can't figure out how to make that work with platformio gcc
// options so this is my quick hack to make things work

#if defined(ARDUINO_ARCH_ESP32)
#define HAS_FREE_RTOS

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

#if defined(ARDUINO_NRF52_ADAFRUIT) || defined(ARDUINO_ARCH_RP2040)
#define HAS_FREE_RTOS

#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <task.h>
#endif

#ifdef HAS_FREE_RTOS

// Include real FreeRTOS defs above

#else

// Include placeholder fake FreeRTOS defs

#include <Arduino.h>

typedef uint32_t TickType_t;
typedef uint32_t BaseType_t;

#define portMAX_DELAY UINT32_MAX

#define tskIDLE_PRIORITY 0
#define configMAX_PRIORITIES 10 // Highest priority level

// Don't do anything on non free rtos platforms when done with the ISR
#define portYIELD_FROM_ISR(x)

enum eNotifyAction { eNoAction, eSetValueWithoutOverwrite, eSetValueWithOverwrite };

#endif