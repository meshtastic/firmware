#pragma once

#ifdef ARCH_NRF52

namespace concurrency
{
class Lock;
/** Shared mutex for SAADC configuration and reads (VDD + battery analog path). */
extern Lock *nrf52SaadcLock;
} // namespace concurrency

#endif
