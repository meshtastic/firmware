#pragma once

/**
 * SafeBoot: pre-init power guard for battery/solar nodes.
 *
 * Runs very early in setup(), before any high-current rail or
 * filesystem write. Reads Vbat with a lightweight ADC sample and:
 *   - if stable above the wake threshold, returns and boot continues;
 *   - otherwise puts the MCU back into deep sleep with hysteresis and
 *     exponential backoff. Wake sources: timer (ESP32), hardware
 *     comparator (nRF52 LPCOMP), or button.
 *
 * State across attempts is kept in non-flash retention memory
 * (ESP32: RTC_NOINIT_ATTR; nRF52: GPREGRET2).
 *
 * Configurability (highest priority first):
 *   1) USERPREFS_POWER_SAFE_BOOT_* (build-time, via userPrefs.jsonc)
 *   2) DEFAULT_SAFE_BOOT_* in variant.h (per hardware)
 *   3) default_safe_boot_* in src/mesh/Default.h (fallback)
 *
 * No-op when ARCH is not ESP32/nRF52, BATTERY_PIN is undefined, or
 * SAFE_BOOT_DISABLED is set.
 */

#include <stdint.h>

namespace SafeBoot
{

/**
 * Run the SafeBoot pre-init power guard.
 *
 * Must be called as early as possible in setup(), right after
 * initDeepSleep() and BEFORE enabling any high-current rail
 * (LoRa TCXO, display rail, GPS, sensors), I2C scan, filesystem
 * writes, NodeDB load, radio/BLE init.
 *
 * If the guard decides the node has insufficient energy to boot
 * safely, this function does NOT return: it places the MCU into
 * deep sleep with a retry timer (and optional hardware-comparator
 * wake on nRF52). Otherwise it returns and normal boot proceeds.
 */
void checkAndMaybeSleep();

/**
 * True iff SafeBoot has decided the node is healthy enough to
 * keep running.

 */
bool isSettled();

/** True iff this boot was a recovery from a SafeBoot-induced sleep. */
bool wokeFromSafeBoot();

/**
 * True iff the previous reset reason indicated a brownout, watchdog
 * or other unclean condition. Consumers may use this to enable
 * defensive behavior (e.g. retry filesystem reads).
 */
bool lastResetWasUnclean();

} // namespace SafeBoot
