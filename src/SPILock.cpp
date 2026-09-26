#include "SPILock.h"
#include "configuration.h"
#include <Arduino.h>
#include <assert.h>

#if defined(ARCH_PORTDUINO)
// Static storage on Portduino, so the lock is usable before initSPI() runs. Lock::lock() was a
// no-op here, which made a null spiLock harmless; now that it takes a real mutex, anything
// reaching the filesystem ahead of initSPI() - a native test constructing NodeDB, say - would
// dereference a null pointer instead. The embedded targets keep the lazy construction below:
// their semaphore cannot be created from a static initializer.
static concurrency::Lock spiLockInstance;
concurrency::Lock *spiLock = &spiLockInstance;

void initSPI() {}
#else
concurrency::Lock *spiLock;

void initSPI()
{
    assert(!spiLock);
    spiLock = new concurrency::Lock();
}
#endif
