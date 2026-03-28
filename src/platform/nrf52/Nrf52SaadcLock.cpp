#include "Nrf52SaadcLock.h"
#include "concurrency/Lock.h"
#include "configuration.h"

#ifdef ARCH_NRF52

namespace concurrency
{
static Lock nrf52SaadcLockInstance;
Lock *nrf52SaadcLock = &nrf52SaadcLockInstance;
} // namespace concurrency

#endif
