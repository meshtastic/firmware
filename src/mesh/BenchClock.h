#pragma once
#include "configuration.h"

// Bench timing in microseconds. Adafruit's nRF52 micros() counts FreeRTOS ticks (976.6 us steps), too coarse for
// radio steps of about a millisecond, so time them on the cycle counter there. Call benchClockStart() once first.
#ifdef ARDUINO_NRF52_ADAFRUIT
inline void benchClockStart()
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}
inline uint32_t benchClock()
{
    return DWT->CYCCNT;
}
inline uint32_t benchClockToUs(uint32_t elapsed)
{
    return elapsed / (SystemCoreClock / 1000000);
}
#else
inline void benchClockStart() {}
inline uint32_t benchClock()
{
    return micros();
}
inline uint32_t benchClockToUs(uint32_t elapsed)
{
    return elapsed;
}
#endif
