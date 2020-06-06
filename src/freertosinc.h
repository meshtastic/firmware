#pragma once

// The FreeRTOS includes are in a different directory on ESP32 and I can't figure out how to make that work with platformio gcc options
// so this is my quick hack to make things work

#ifdef ARDUINO_ARCH_ESP32
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#else
// not yet supported on cubecell
#ifndef CubeCell_BoardPlus
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>
#endif
#endif