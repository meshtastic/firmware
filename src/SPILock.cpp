#include "SPILock.h"
#include "configuration.h"
#include <Arduino.h>
#include <assert.h>

concurrency::Lock *spiLock;
#ifdef MESHTASTIC_SPI_CMD_ATOMIC
concurrency::Lock *spiCmdLock;
volatile uint32_t spiCmdContended;
#endif

void initSPI()
{
    assert(!spiLock);
    spiLock = new concurrency::Lock();
#ifdef MESHTASTIC_SPI_CMD_ATOMIC
    spiCmdLock = new concurrency::Lock();
#endif
}