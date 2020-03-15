#include "debug.h"

#include <cstdint>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "configuration.h"

namespace meshtastic
{

void printThreadInfo(const char *extra)
{
    uint32_t taskHandle = reinterpret_cast<uint32_t>(xTaskGetCurrentTaskHandle());
    DEBUG_MSG("printThreadInfo(%s) task: %" PRIx32 " core id: %u min free stack: %u\n", extra, taskHandle, xPortGetCoreID(),
              uxTaskGetStackHighWaterMark(nullptr));
}

} // namespace meshtastic
