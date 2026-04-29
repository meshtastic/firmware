#pragma once

#ifdef MESHTASTIC_ENABLE_APPROTECT

/**
 * Enable APPROTECT on nRF52840 to permanently disable the SWD/JTAG debug port.
 *
 * Writes NRF_UICR->APPROTECT = 0x00 (and ERASEPROTECT/DEBUG variants where
 * applicable) if not already set, then triggers a reset so the change takes
 * effect. Must be called early in setup(), before any sensitive data is
 * loaded into RAM, so an attacker who powered the device cannot halt it via
 * SWD before the lock is in place.
 *
 * THIS IS PERMANENT AND IRREVERSIBLE.
 *
 * Once APPROTECT is written:
 *   - SWD/JTAG halt, memory read, and register access are blocked forever
 *     for the life of this UICR.
 *   - The lock survives reboot, power cycle, and ordinary firmware reflash.
 *   - The ONLY way to regain debug access is `nrfjprog --eraseall` (or an
 *     equivalent CTRL-AP ERASEALL via SWD), which wipes the entire flash —
 *     including bootloader, application, LittleFS, and the encrypted DEK.
 *     A successful erase therefore destroys all on-device state, which is
 *     intentional: it prevents an attacker from clearing APPROTECT to
 *     extract user data.
 *   - The USB/DFU bootloader path still works for routine firmware updates,
 *     because the bootloader does not require SWD.
 *
 * Do not enable this on a device you may want to debug later. There is no
 * recovery short of full chip erase.
 */
void enableAPProtect();

#endif // MESHTASTIC_ENABLE_APPROTECT
