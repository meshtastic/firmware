#pragma once

#ifdef SENSECAP_INDICATOR

#include "SPILock.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Set by the TFT task while it holds spiLock around the LVGL handler. The
// link round trips below run inside LVGL callbacks (map tiles, keyboard
// scan) and would otherwise hold the SPI bus - which the LoRa radio shares
// with the display - for the duration of a request.
extern volatile TaskHandle_t spiLockHolder;

/**
 * Hands the SPI bus back for the duration of an interdevice link round
 * trip. Nothing in a round trip touches SPI, and a slow or unresponsive
 * co-processor would otherwise starve the radio for the request timeout.
 *
 * A no-op unless the current task is the one that holds spiLock, so it is
 * safe to use from any context (and nests harmlessly).
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
