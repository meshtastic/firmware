#pragma once

#ifdef SENSECAP_INDICATOR

#include "SPILock.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// set by the TFT task while it holds spiLock around the LVGL handler
extern volatile TaskHandle_t spiLockHolder;

/**
 * Hands the SPI bus (shared with the LoRa radio) back for the duration of a
 * link round trip. No-op unless the current task holds spiLock.
 */
class SpiLockBreak
{
  public:
    SpiLockBreak()
    {
        held = spiLockHolder == xTaskGetCurrentTaskHandle();
        if (held) {
            spiLockHolder = nullptr;
            spiLock->unlock();
        }
    }
    ~SpiLockBreak()
    {
        if (held) {
            spiLock->lock();
            spiLockHolder = xTaskGetCurrentTaskHandle();
        }
    }

  private:
    bool held;
};

#endif // SENSECAP_INDICATOR
