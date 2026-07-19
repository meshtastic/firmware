#pragma once

#include <stdint.h>

/**
 * A randomly generated, locally administered MAC address persisted in the
 * filesystem. It gives the device a random link-layer identity (BLE address,
 * node number, default names) that is stable across reboots but re-rolled by
 * factory reset, which removes /prefs.
 *
 * Opt-in: compiled out unless USERPREFS_RANDOM_DEVICE_ID is set (userPrefs.jsonc or
 * a variant define), in which case platforms without support fall back to the
 * hardware address.
 *
 * @return false if disabled, or no random MAC could be loaded or generated;
 *         callers should fall back to the hardware address.
 */
bool persistedRandomDeviceIdGet(uint8_t out[6]);

/**
 * Generate, persist, and cache a fresh random MAC. Called during factory reset
 * after /prefs has been wiped, so the device comes back with a new identity.
 */
void persistedRandomDeviceIdRegenerate();
