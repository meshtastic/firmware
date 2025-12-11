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

#if (defined(ARDUINO_NRF52_ADAFRUIT) || defined(ARDUINO_ARCH_RP2040)) && !defined(__PLAT_RP2350__)
// RP2040 and NRF52: Use real FreeRTOS (excludes RP2350 which uses std::queue)
#define HAS_FREE_RTOS

#include <FreeRTOS.h>
#include <queue.h>
#include <semphr.h>
#include <task.h>
#endif

// RP2350 with SDK 5.4.3: Provide FreeRTOS stubs WITHOUT scheduler
// (SDK 5.4.3 scheduler conflicts with manual Core1 launch via multicore_launch_core1)
// Uses std::queue fallback from TypedQueue.h:65-124 instead of real FreeRTOS queues
#if defined(__PLAT_RP2350__)
// Don't define HAS_FREE_RTOS - this triggers std::queue fallback

#include <Arduino.h>

// Basic FreeRTOS types
typedef uint32_t TickType_t;
typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;

// FreeRTOS constants
#define portMAX_DELAY UINT32_MAX
#define pdTRUE  ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define pdPASS  pdTRUE
#define pdFAIL  pdFALSE

// Task priorities
#define tskIDLE_PRIORITY 0
#define configMAX_PRIORITIES 10

// ISR macros (no-op on std::queue fallback)
#define portYIELD_FROM_ISR(x)

// Notification actions
enum eNotifyAction { eNoAction, eSetValueWithoutOverwrite, eSetValueWithOverwrite };

// Semaphore/Lock types (Meshtastic uses concurrency::Lock wrapper, not raw FreeRTOS)
typedef void* SemaphoreHandle_t;
typedef void* StaticSemaphore_t;

#endif

#ifdef HAS_FREE_RTOS

// Include real FreeRTOS defs above

#elif !defined(__PLAT_RP2350__)

// Include placeholder fake FreeRTOS defs (for non-FreeRTOS, non-RP2350 platforms)

#include <Arduino.h>

typedef uint32_t TickType_t;
typedef uint32_t BaseType_t;

#define portMAX_DELAY UINT32_MAX

#define tskIDLE_PRIORITY 0
#define configMAX_PRIORITIES 10 // Highest priority level

// Don't do anything on non free rtos platforms when done with the ISR
#define portYIELD_FROM_ISR(x)

enum eNotifyAction { eNoAction, eSetValueWithoutOverwrite, eSetValueWithOverwrite };

#endif // HAS_FREE_RTOS or !__PLAT_RP2350__