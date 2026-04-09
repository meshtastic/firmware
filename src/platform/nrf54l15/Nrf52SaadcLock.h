// Nrf52SaadcLock.h — stub for nRF54L15/Zephyr
// Power.cpp includes this when ARCH_NRF52 is defined.
// Phase 2: compile-only stub.
#pragma once

#ifdef ARCH_NRF52

#include "concurrency/Lock.h"

namespace concurrency {
/** Shared mutex for SAADC configuration and reads (VDD + battery analog path).
 *  On nRF54L15 ADC is handled differently; this is a compile-only stub. */
extern Lock *nrf52SaadcLock;
} // namespace concurrency

#endif
