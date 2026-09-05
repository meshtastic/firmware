#pragma once

#include "configuration.h"

#ifdef ARCH_NRF52

#include <stdint.h>

// Advances the NonBlockingRTTTL sequencer from a FreeRTOS timer, so the next note does not wait
// behind the cooperative main loop. tone() is hardware timed, so only note starts need servicing.
namespace NRF52RtttlTicker
{
void begin(uint8_t pin, const char *song);

// Only advances the song if the timer could not be created or started; otherwise a no-op.
void pump();

void stop();
} // namespace NRF52RtttlTicker

#endif
