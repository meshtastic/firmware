#pragma once

#ifdef MESHTASTIC_ENABLE_APPROTECT

/**
 * Enable APPROTECT on nRF52840 to disable the SWD/JTAG debug port.
 *
 * Writes NRF_UICR->APPROTECT = 0x00 (and ERASEPROTECT/DEBUG variants where
 * applicable) if not already set, then triggers a reset so the change takes
 * effect. Must be called early in setup(), before any sensitive data is
 * loaded into RAM, so an attacker who powered the device cannot halt it via
 * SWD before the lock is in place.
 *
 * Once APPROTECT is written:
 *   - SWD/JTAG halt, memory read, and register access are blocked.
 *   - The lock survives reboot, power cycle, and ordinary USB/DFU firmware
 *     reflash. The DFU bootloader path keeps working for routine app
 *     updates because the bootloader doesn't need SWD.
 *   - The only way to clear APPROTECT is an SWD-side `nrfjprog --recover`
 *     (CTRL-AP ERASEALL), which wipes the entire chip - bootloader,
 *     application, LittleFS, and the encrypted DEK - destroying all
 *     on-device state in the process. That destructive coupling is the
 *     point: an attacker cannot clear APPROTECT to extract user data
 *     without also wiping the data they were trying to read.
 *
 * Practical implication: do not enable this on a device you might want to
 * SWD-debug later. Recovery is possible but always destroys all user
 * data; routine USB reflashing alone will NOT clear it.
 */
void enableAPProtect();

#endif // MESHTASTIC_ENABLE_APPROTECT
