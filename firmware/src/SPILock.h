#pragma once

#include "../concurrency/LockGuard.h"

/**
 * Used to provide mutual exclusion for access to the SPI bus.  Usage:
 * concurrency::LockGuard g(spiLock);
 */
extern concurrency::Lock *spiLock;

/** Setup SPI access and create the spiLock lock. */
void initSPI();