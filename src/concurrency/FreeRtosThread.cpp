#include "FreeRtosThread.h"

#ifdef HAS_FREE_RTOS

#include <assert.h>

#ifdef ARDUINO_ARCH_ESP32
#include "esp_task_wdt.h"
#endif

namespace concurrency
{

void FreeRtosThread::start(const char *name, size_t stackSize, uint32_t priority)
{
    auto r = xTaskCreate(callRun, name, stackSize, this, priority, &taskHandle);
    assert(r == pdPASS);
}

void FreeRtosThread::serviceWatchdog()
{
#ifdef ARDUINO_ARCH_ESP32
    esp_task_wdt_reset();
#endif
}

void FreeRtosThread::startWatchdog()
{
#ifdef ARDUINO_ARCH_ESP32
    auto r = esp_task_wdt_add(taskHandle);
    assert(r == ESP_OK);
#endif
}

void FreeRtosThread::stopWatchdog()
{
#ifdef ARDUINO_ARCH_ESP32
    auto r = esp_task_wdt_delete(taskHandle);
    assert(r == ESP_OK);
#endif
}

} // namespace concurrency

#endif