#pragma once

#include "../concurrency/LockGuard.h"

/**
 * Used to provide mutual exclusion for access to the SPI bus.  Usage:
 * concurrency::LockGuard g(spiLock);
 */
extern concurrency::Lock *spiLock;

#ifdef MESHTASTIC_SPI_CMD_ATOMIC
/**
 * Bench: held for a whole radio-chip command, where spiLock is held only for each SPI transfer. An LRxxxx
 * register read is two transfers with a BUSY wait between them, and Module::SPItransferStream drops the
 * transaction before that wait -- so another thread's command can land mid-read. Taken outside spiLock,
 * never nested inside it.
 */
extern concurrency::Lock *spiCmdLock;

/// How many command acquisitions found the lock already held -- i.e. how often the two threads really did
/// collide inside a chip command. 0 would mean the race this lock closes never happens.
extern volatile uint32_t spiCmdContended;
#endif

/** Setup SPI access and create the spiLock lock. */
void initSPI();