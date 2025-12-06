#include "SPILock.h"
#include "configuration.h"
#include <Arduino.h>
#include <assert.h>

concurrency::Lock *spiLock;

void initSPI()
{
    assert(!spiLock);
    spiLock = new concurrency::Lock();
}