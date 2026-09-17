#include "UptimeClock.h"
#include "configuration.h"
#include "mesh/Throttle.h"
#include <InternalFileSystem.h>
#include <SPI.h>
#include <Wire.h>

// The nRF54L core compiles the nrfx 3 drivers itself: errno-style returns, 0 is success
#include <nrfx_wdt.h>

#include <assert.h>
#include <ble_gap.h>
#include <memory.h>
#include <stdio.h>

#include "HardwareRNG.h"
#include "NodeDB.h"
#include "Nrf52SaadcLock.h"
#include "Power.h"
#include "PowerMon.h"
#include "concurrency/LockGuard.h"
#include "error.h"
#include "main.h"
#include "meshUtils.h"
#include <power/PowerHAL.h>

#define APP_WATCHDOG_SECS 90

// THRESHOLD + HYSTERESIS must stay below the regulated VDD of the board (3.0 or 3.3V)
#ifndef SAFE_VDD_VOLTAGE_THRESHOLD
#define SAFE_VDD_VOLTAGE_THRESHOLD 2.7
#endif

#ifndef SAFE_VDD_VOLTAGE_THRESHOLD_HYST
#define SAFE_VDD_VOLTAGE_THRESHOLD_HYST 0.2
#endif

uint16_t getVDDVoltage();

// Weak variant hooks. noinline: weak default and call site are in this file, so LTO would
// inline the empty body and drop the variant's override.
__attribute__((noinline)) void variant_shutdown() __attribute__((weak));
__attribute__((noinline)) void variant_shutdown() {}

__attribute__((noinline)) void variant_nrf52LoopHook(void) __attribute__((weak));
__attribute__((noinline)) void variant_nrf52LoopHook(void) {}

static nrfx_wdt_t nrfx_wdt = NRFX_WDT_INSTANCE(NRF_WDT31);
static nrfx_wdt_channel_id nrfx_wdt_channel_id_main;

// Public so a debugger can clear it. The flash driver depends on the SoftDevice, so the
// filesystem stops working as well.
bool useSoftDevice = true;

bool powerHAL_isVBUSConnected()
{
    return false; // no USB peripheral
}

bool powerHAL_isPowerLevelSafe()
{
    static bool powerLevelSafe = true;

#ifdef SAFE_VDD_VOLTAGE_THRESHOLD_MV
    uint16_t threshold = SAFE_VDD_VOLTAGE_THRESHOLD_MV;
#else
    uint16_t threshold = (uint16_t)(SAFE_VDD_VOLTAGE_THRESHOLD * 1000.0f + 0.5f); // convert V to mV
#endif
#ifdef SAFE_VDD_VOLTAGE_THRESHOLD_HYST_MV
    uint16_t hysteresis = SAFE_VDD_VOLTAGE_THRESHOLD_HYST_MV;
#else
    uint16_t hysteresis = (uint16_t)(SAFE_VDD_VOLTAGE_THRESHOLD_HYST * 1000.0f + 0.5f);
#endif

    if (powerLevelSafe) {
        if (getVDDVoltage() < threshold) {
            powerLevelSafe = false;
        }
    } else {
        // power level is only safe again when it raises above threshold + hysteresis
        if (getVDDVoltage() >= (threshold + hysteresis)) {
            powerLevelSafe = true;
        }
    }

    return powerLevelSafe;
}

void powerHAL_platformInit()
{
    // remember to always match VBAT_AR_INTERNAL with AREF_VALUE in variant definition file
#ifdef VBAT_AR_INTERNAL
    analogReference(VBAT_AR_INTERNAL);
#else
    analogReference(AR_INTERNAL); // 3.6V
#endif
}

// get VDD voltage (in millivolts)
uint16_t getVDDVoltage()
{
    concurrency::LockGuard guard(concurrency::nrf52SaadcLock);

    // Match battery read resolution; SAADC is shared with AnalogBatteryLevel in Power.cpp.
    analogReadResolution(BATTERY_SENSE_RESOLUTION_BITS);

    // VDD is 1.8-3.3V, remap the analog reference to 3.6V
    analogReference(AR_INTERNAL);

    uint16_t vddADCRead = analogReadVDD();
    float voltage = ((1000 * 3.6) / pow(2, BATTERY_SENSE_RESOLUTION_BITS)) * vddADCRead;

// restore default battery reading reference
#ifdef VBAT_AR_INTERNAL
    analogReference(VBAT_AR_INTERNAL);
#endif

    return voltage;
}

bool loopCanSleep()
{
    return !Serial;
}

// handle standard gcc assert failures
void __attribute__((noreturn)) __assert_func(const char *file, int line, const char *func, const char *failedexpr)
{
    LOG_ERROR("assert failed %s: %d, %s, test=%s", file, line, func, failedexpr);
    Serial.flush(); // the reset below would cut the message short
    NVIC_SystemReset();
}

void getMacAddr(uint8_t *dmac)
{
    const uint8_t *src = (const uint8_t *)NRF_FICR->DEVICEADDR;
    dmac[5] = src[0];
    dmac[4] = src[1];
    dmac[3] = src[2];
    dmac[2] = src[3];
    dmac[1] = src[4];
    dmac[0] = src[5] | 0xc0; // MSB high two bits get set elsewhere in the bluetooth stack
}

bool getDeviceId(uint8_t *deviceId)
{
    // Nordic burns a FIPS-compliant random id into each chip at the factory. We concatenate
    // the device address to that random id to form the 16-byte hardware identifier.
    uint64_t device_id_start = ((uint64_t)NRF_FICR->INFO.DEVICEID[1] << 32) | NRF_FICR->INFO.DEVICEID[0];
    uint64_t device_id_end = ((uint64_t)NRF_FICR->DEVICEADDR[1] << 32) | NRF_FICR->DEVICEADDR[0];
    memcpy(deviceId, &device_id_start, sizeof(device_id_start));
    memcpy(deviceId + sizeof(device_id_start), &device_id_end, sizeof(device_id_end));
    return true;
}

#if !MESHTASTIC_EXCLUDE_BLUETOOTH
void setBluetoothEnable(bool enable)
{
    // For debugging use: don't use bluetooth
    if (!useSoftDevice) {
        if (enable)
            LOG_INFO("Disable NRF52 BLUETOOTH WHILE DEBUGGING");
        return;
    }

    // If user disabled bluetooth: init then disable advertising & reduce power
    // Workaround. Avoid issue where device hangs several days after boot..
    // Allegedly, no significant increase in power consumption
    if (!config.bluetooth.enabled) {
        static bool initialized = false;
        if (!initialized) {
            nrf52Bluetooth = new NRF52Bluetooth();
            nrf52Bluetooth->startDisabled();
            initialized = true;
        }
        return;
    }

    if (enable) {
        powerMon->setState(meshtastic_PowerMon_State_BT_On);

        // If not yet set-up
        if (!nrf52Bluetooth) {
            LOG_DEBUG("Init NRF52 Bluetooth");
            nrf52Bluetooth = new NRF52Bluetooth();
            nrf52Bluetooth->setup();
        }
        // Already setup, apparently
        else
            nrf52Bluetooth->resumeAdvertising();
    }
    // Disable (if previously set-up)
    else if (nrf52Bluetooth) {
        powerMon->clearState(meshtastic_PowerMon_State_BT_On);
        nrf52Bluetooth->shutdown();
    }
}
#else
#warning NRF52 "Bluetooth disable" workaround does not apply to builds with MESHTASTIC_EXCLUDE_BLUETOOTH
void setBluetoothEnable(bool enable) {}
#endif

/**
 * Override printf to use the SEGGER output library (note - this does not effect the printf method on the debug console)
 */
int printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    auto res = SEGGER_RTT_vprintf(0, fmt, &args);
    va_end(args);
    return res;
}

namespace
{
constexpr uint8_t NRF52_MAGIC_LFS_IS_CORRUPT = 0xF5;
constexpr uint32_t MULTIPLE_CORRUPTION_DELAY_MILLIS = 20 * 60 * 1000;
// When the last format happened, not when the next one is due: measuring forward from the event
// bounds the pause below by the constant, where a stored deadline could hand delay() any value.
// Armed separately because preFSBegin() runs in the first millisecond of boot, so a zero timestamp
// is a legitimate value here, not an "unset" marker.
static uint32_t last_format_ms = 0;
static bool formatted_this_boot = false;

// Report the critical error from loop(), giving a chance for the screen to be initialized first.
inline void reportLittleFSCorruptionOnce()
{
    static bool report_corruption = formatted_this_boot;
    if (report_corruption) {
        report_corruption = false;
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_FLASH_CORRUPTION_UNRECOVERABLE);
    }
}
} // namespace

void preFSBegin()
{
    // GPREGRET keeps its value across warm boots. Check that this is a warm boot and, if GPREGRET
    // is set to NRF52_MAGIC_LFS_IS_CORRUPT, format LittleFS.
    if (!(NRF_RESET->RESETREAS == 0 && NRF_POWER->GPREGRET[0] == NRF52_MAGIC_LFS_IS_CORRUPT))
        return;
    NRF_POWER->GPREGRET[0] = 0;
    // unset-sentinel-ok: formatted_this_boot carries the armed state, so 0 is a legal stamp
    last_format_ms = Time::getMillis();
    formatted_this_boot = true;
    InternalFS.format();
    LOG_INFO("LittleFS format complete; restoring default settings");
}

extern "C" void lfs_assert(const char *reason)
{
    LOG_ERROR("LittleFS corruption detected: %s", reason);
    // Test the armed flag first, since elapsed-since-0 is inside the backoff for the first 20
    // minutes after each wrap.
    if (formatted_this_boot && Throttle::isWithinTimespanMs(last_format_ms, MULTIPLE_CORRUPTION_DELAY_MILLIS)) {
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_FLASH_CORRUPTION_UNRECOVERABLE);
        // Same clock Throttle just read, and clamped: the check above and a second, later read
        // can straddle the backoff, which would wrap the remainder into a ~50-day delay().
        const uint32_t elapsed = Time::getMillis() - last_format_ms;
        const uint32_t millis_remain =
            elapsed < MULTIPLE_CORRUPTION_DELAY_MILLIS ? MULTIPLE_CORRUPTION_DELAY_MILLIS - elapsed : 0;
        LOG_WARN("Pausing %u seconds to avoid wear on flash storage", millis_remain / 1000);
        delay(millis_remain);
    }
    LOG_INFO("Rebooting to format LittleFS");
    delay(500); // Give the serial port a bit of time to output that last message.

    // POFWARN means flash undervoltage protection fired, not data corruption; a plain reboot
    // waits for a safe power level again.
    if (!NRF_POWER->EVENTS_POFWARN) {
        // The SoftDevice call fails when the SoftDevice is not enabled yet
        if (!(sd_power_gpregret_clr(0, 0xFF) == NRF_SUCCESS &&
              sd_power_gpregret_set(0, NRF52_MAGIC_LFS_IS_CORRUPT) == NRF_SUCCESS)) {
            NRF_POWER->GPREGRET[0] = NRF52_MAGIC_LFS_IS_CORRUPT;
        }
    }

    NVIC_SystemReset();
}

// Defined by the core's InternalFileSystem, completes a pending sd_flash_write()
extern "C" void flash_nrf5x_event_cb(uint32_t event);

// s145 asks the application for entropy. Bluefruit's SoC task answers the same request, but whichever
// consumer pops the event must seed, so the request is never dropped. HardwareRNG::fill() always fills.
static void seedSoftDevice()
{
    uint8_t seed[SD_RAND_SEED_SIZE];
    HardwareRNG::fill(seed, sizeof(seed));
    uint32_t err = sd_rand_seed_set(seed);
    if (err != NRF_SUCCESS)
        LOG_WARN("sd_rand_seed_set failed: %u", err);
}

void checkSDEvents()
{
    if (useSoftDevice) {
        uint32_t evt;
        while (NRF_SUCCESS == sd_evt_get(&evt)) {
            switch (evt) {
            case NRF_EVT_POWER_FAILURE_WARNING:
                RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_BROWNOUT);
                break;
            // Bluefruit's SoC task polls the same queue; an event taken here must still reach the flash driver
            case NRF_EVT_FLASH_OPERATION_SUCCESS:
            case NRF_EVT_FLASH_OPERATION_ERROR:
                flash_nrf5x_event_cb(evt);
                break;
            case NRF_EVT_RAND_SEED_REQUEST:
                seedSoftDevice();
                break;

            default:
                LOG_DEBUG("Unexpected SDevt %d", evt);
                break;
            }
        }
    } else {
        if (NRF_POWER->EVENTS_POFWARN)
            RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_BROWNOUT);
    }
}

void nrf52Loop()
{
    {
        static bool watchdog_running = false;
        if (!watchdog_running) {
            nrfx_wdt_enable(&nrfx_wdt);
            watchdog_running = true;
        }
    }
    nrfx_wdt_channel_feed(&nrfx_wdt, nrfx_wdt_channel_id_main);

    checkSDEvents();
    reportLittleFSCorruptionOnce();

    variant_nrf52LoopHook();
}

void nrf52Setup()
{
    // The core caches RESETREAS in init() and clears the register before setup() runs
    uint32_t why = readResetReason();
    LOG_DEBUG("Reset reason: 0x%x", why);

    // Recommended setting for Monitor Mode Debugging
    NVIC_SetPriority(DebugMonitor_IRQn, 6UL);

    // Init random seed
    uint32_t seed = 0;
    if (!HardwareRNG::seed(seed)) {
        LOG_WARN("Hardware RNG seed unavailable, using PRNG fallback");
        // Use a hardware timer value as a fallback seed for better entropy
        seed = micros();
    }
    LOG_DEBUG("Set random seed %u", seed);
    randomSeed(seed);

    // Set up nrfx watchdog. Do not enable the watchdog yet (we do that
    // the first time through the main loop), so that other threads can
    // allocate their own wdt channel to protect themselves from hangs.
    // behaviour is a RUN_* mask, 0 pauses the watchdog in sleep and halt
    nrfx_wdt_config_t wdt0_config = {.behaviour = 0, .reload_value = APP_WATCHDOG_SECS * 1000};
    int r = nrfx_wdt_init(&nrfx_wdt, &wdt0_config, nullptr, nullptr);
    assert(r == 0);

    r = nrfx_wdt_channel_alloc(&nrfx_wdt, &nrfx_wdt_channel_id_main);
    assert(r == 0);
}

void cpuDeepSleep(uint32_t msecToWake)
{
#if HAS_WIRE
    Wire.end();
#endif
    SPI.end();
#if SPI_INTERFACES_COUNT > 1
    SPI1.end();
#endif
    if (Serial)
        Serial.end();
#ifdef PIN_SERIAL1_RX
    if (Serial1)
        Serial1.end();
#endif

    setBluetoothEnable(false);

    // Run shutdown code if specified in variant.cpp
    variant_shutdown();

    // Sleepy trackers or sensors can low power "sleep"
    // Don't enter this if we're sleeping portMAX_DELAY, since that's a shutdown event
    if (msecToWake != portMAX_DELAY &&
        (IS_ONE_OF(config.device.role, meshtastic_Config_DeviceConfig_Role_TRACKER,
                   meshtastic_Config_DeviceConfig_Role_TAK_TRACKER, meshtastic_Config_DeviceConfig_Role_SENSOR) &&
         config.power.is_power_saving == true)) {
        sd_power_mode_set(NRF_POWER_MODE_LOWPWR);
        delay(msecToWake);
        NVIC_SystemReset();
    } else {
        // Resume on user button press; the bootloader skips DFU on this magic
        constexpr uint32_t DFU_MAGIC_SKIP = 0x6d;
        sd_power_gpregret_clr(0, 0xFF);
        sd_power_gpregret_set(0, DFU_MAGIC_SKIP);

        // s145 has no sd_power_system_off(); REGULATORS is not SoftDevice-restricted
        NRF_REGULATORS->SYSTEMOFF = 1;
    }

    // The following code should not be run, because we are off
    while (1) {
        delay(5000);
        LOG_DEBUG(".");
    }
}

void clearBonds()
{
    if (!nrf52Bluetooth) {
        nrf52Bluetooth = new NRF52Bluetooth();
        nrf52Bluetooth->setup();
    }
    nrf52Bluetooth->clearBonds();
}

void enterDfuMode()
{
    enterSerialDfu(); // no USB, so no UF2 bootloader
}
