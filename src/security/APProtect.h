#pragma once

#ifdef MESHTASTIC_ENABLE_APPROTECT

/**
 * Enable APPROTECT on nRF52840 to block debug probe access.
 * Writes NRF_UICR->APPROTECT = 0x00 if not already set.
 * Must be called early in setup() before any sensitive data is loaded.
 *
 * WARNING: This is a one-way operation. Once APPROTECT is enabled,
 * the debug port is permanently disabled. The device can still be
 * re-flashed via the bootloader (USB/DFU), but SWD/JTAG access is blocked.
 */
void enableAPProtect();

#endif // MESHTASTIC_ENABLE_APPROTECT
