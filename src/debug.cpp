#include "debug.h"

#include <cstdint>

#include "freertosinc.h"
#include "configuration.h"

namespace meshtastic
{

void printThreadInfo(const char *extra)
{
#ifndef NO_ESP32
    uint32_t taskHandle = reinterpret_cast<uint32_t>(xTaskGetCurrentTaskHandle());
    DEBUG_MSG("printThreadInfo(%s) task: %" PRIx32 " core id: %u min free stack: %u\r\n", extra, taskHandle, xPortGetCoreID(),
              uxTaskGetStackHighWaterMark(nullptr));
#endif
}

} // namespace meshtastic
