/*
 * main-nrf54l15.cpp — Platform entry points for Nordic nRF54L15
 *
 * Adapted from src/platform/nrf52/main-nrf52.cpp.
 * SoftDevice, Adafruit BLE, and nRFCrypto are NOT available on nRF54L15.
 * Phase 2 will add proper BLE via Zephyr MPSL APIs.
 *
 * TODO items are marked with "TODO(nrf54l15):"
 */

#include "configuration.h"
#include <SPI.h>
#include <Wire.h>
#include <assert.h>
#include <stdio.h>

#include "NodeDB.h"
#include "PowerMon.h"
#include "Router.h"
#include "error.h"
#include "main.h"
#include "mesh/MeshService.h"
#include "meshUtils.h"
#include "power.h"
#include <power/PowerHAL.h>

// ── Watchdog ──────────────────────────────────────────────────────────────
// TODO(nrf54l15): nRF54L15 has a WDT peripheral but nrfx_wdt driver support
// may differ depending on the Zephyr SDK version. Enable once confirmed.
#define APP_WATCHDOG_SECS 90
static bool watchdog_running = false;

static inline void watchdog_feed() {} // TODO(nrf54l15): replace with real WDT feed

// ── Weak variant hooks ────────────────────────────────────────────────────
void variant_shutdown() __attribute__((weak));
void variant_shutdown() {}

void variant_nrf54l15LoopHook(void) __attribute__((weak));
void variant_nrf54l15LoopHook(void) {}

// ── PowerHAL ─────────────────────────────────────────────────────────────
bool powerHAL_isVBUSConnected()
{
    // TODO(nrf54l15): nRF54L15 has a USB POWER peripheral — read USBREGSTATUS
    return false;
}

bool powerHAL_isPowerLevelSafe()
{
    // TODO(nrf54l15): implement SAADC VDD measurement similar to nRF52
    return true;
}

void powerHAL_platformInit()
{
    // TODO(nrf54l15): configure POF comparator and analog reference if needed
}

// ── Utilities ─────────────────────────────────────────────────────────────
bool loopCanSleep()
{
    return !Serial;
}

void updateBatteryLevel(uint8_t level)
{
    (void)level;
}

void __attribute__((noreturn)) __assert_func(const char *file, int line, const char *func, const char *failedexpr)
{
    LOG_ERROR("assert failed %s: %d, %s, test=%s", file, line, func, failedexpr);
    NVIC_SystemReset();
}

void getMacAddr(uint8_t *dmac)
{
    // TODO(nrf54l15): verify FICR register layout for nRF54L15.
    // nRF52840 uses NRF_FICR->DEVICEADDR[0/1]; nRF54L15 Zephyr HAL may differ.
#if defined(NRF_FICR)
    const uint8_t *src = (const uint8_t *)NRF_FICR->DEVICEADDR;
    dmac[5] = src[0];
    dmac[4] = src[1];
    dmac[3] = src[2];
    dmac[2] = src[3];
    dmac[1] = src[4];
    dmac[0] = src[5] | 0xc0;
#else
    // Fallback: fixed placeholder until Zephyr FICR path is confirmed
    dmac[0] = 0xC2;
    dmac[1] = 0xA7;
    dmac[2] = 0x54;
    dmac[3] = 0x15;
    dmac[4] = 0x00;
    dmac[5] = 0x01;
#endif
}

// ── Bluetooth ─────────────────────────────────────────────────────────────────

void setBluetoothEnable(bool enable)
{
    if (enable) {
        static bool initialized = false;
        if (!initialized) {
            nrf54l15Bluetooth = new NRF54L15Bluetooth();
            nrf54l15Bluetooth->startDisabled();
            initialized = true;
        }
        if (nrf54l15Bluetooth) {
            nrf54l15Bluetooth->resumeAdvertising();
        }
    } else {
        if (nrf54l15Bluetooth) {
            nrf54l15Bluetooth->shutdown();
        }
    }
}

void clearBonds()
{
    if (!nrf54l15Bluetooth) {
        nrf54l15Bluetooth = new NRF54L15Bluetooth();
        nrf54l15Bluetooth->setup();
    }
    nrf54l15Bluetooth->clearBonds();
}

void enterDfuMode()
{
    // TODO(nrf54l15): nRF54L15 uses nRF Connect DFU (MCUboot/SUIT).
    // Trigger via Zephyr boot_request_upgrade() or similar.
    NVIC_SystemReset();
}

// ── printf via RTT ────────────────────────────────────────────────────────
// TODO(nrf54l15): SEGGER_RTT may not be available with Zephyr; use printk()
// or a USB CDC console instead. Remove this override if it conflicts.
#ifdef SEGGER_RTT_PRINTF
int printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    auto res = SEGGER_RTT_vprintf(0, fmt, &args);
    va_end(args);
    return res;
}
#endif

// ── Deep sleep ────────────────────────────────────────────────────────────
void cpuDeepSleep(uint32_t msecToWake)
{
#if HAS_WIRE
    Wire.end();
#endif
    SPI.end();
    if (Serial)
        Serial.end();

    variant_shutdown();

    // TODO(nrf54l15): use Zephyr pm_system_suspend() or WFI for proper low-power
    if (msecToWake != portMAX_DELAY) {
        delay(msecToWake);
        NVIC_SystemReset();
    } else {
        // System off equivalent — halt
        while (1) {
            __WFI();
        }
    }
}

// ── Setup / Loop ──────────────────────────────────────────────────────────
// Forward declaration — defined in NRF54L15Bluetooth.cpp
void nrf54l15_bt_preinit();

void nrf54l15Setup()
{
    // nRF54L15 power peripheral layout differs from nRF52; RESETREAS not present here.
    // TODO(Phase 3): use zephyr/drivers/hwinfo.h hwinfo_get_reset_cause()
    LOG_DEBUG("Reset reason: (nRF54L15 power peripheral differs from nRF52, skipped)");

    // TODO(nrf54l15): init SAADC, watchdog, and random seed via nrfx or Zephyr
    // For now seed with a fixed value; replace with hardware entropy source.
#if defined(NRF_FICR)
    randomSeed(analogRead(0) ^ (uint32_t)NRF_FICR->DEVICEADDR[0]);
#else
    randomSeed(analogRead(0));
#endif

    // Pre-initialize BT stack here on the main thread (CONFIG_MAIN_STACK_SIZE=8192).
    // bt_enable() overflows the smaller PowerFSMThread stack when called later.
    // NRF54L15Bluetooth::setup() checks bt_initialized and skips bt_enable() if true.
    nrf54l15_bt_preinit();
}

void nrf54l15Loop()
{
    // First-call gate for the future WDT init — body will hold real init code, not just the bookkeeping flag.
    // cppcheck-suppress duplicateConditionalAssign
    if (!watchdog_running) {
        // TODO(nrf54l15): enable WDT here
        watchdog_running = true;
    }
    watchdog_feed();

    variant_nrf54l15LoopHook();
}
