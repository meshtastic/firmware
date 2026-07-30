#include "configuration.h"
#include <Adafruit_TinyUSB.h>
#include <Adafruit_nRFCrypto.h>
#include <InternalFileSystem.h>
#include <SPI.h>
#include <Wire.h>

#define APP_WATCHDOG_SECS 90
#define NRFX_WDT_ENABLED 1
#define NRFX_WDT0_ENABLED 1
#define NRFX_WDT_CONFIG_NO_IRQ 1
#include "nrfx_power.h"
#include <assert.h>
#include <ble_gap.h>
#include <memory.h>
#include <nrfx_wdt.c>
#include <nrfx_wdt.h>
#include <stdio.h>
// #include <Adafruit_USBD_Device.h>
#include "HardwareRNG.h"
#include "NodeDB.h"
#include "Power.h"
#include "PowerMon.h"
#include "error.h"
#include "main-nrf52.h"
#include "main.h"
#include "meshUtils.h"
#include <power/PowerHAL.h>

#include "Nrf52SaadcLock.h"
#include "concurrency/LockGuard.h"
#include <hal/nrf_lpcomp.h>

#ifdef BQ25703A_ADDR
#include "BQ25713.h"
#endif

// WARNING! THRESHOLD + HYSTERESIS should be less than regulated VDD voltage - which depends on board
// and is 3.0 or 3.3V. Also VDD likes to read values like 2.9999 so make sure you account for that
// otherwise board will not boot at all. Before you modify this part - please triple read NRF52840 power design
// section in datasheet and you understand how REG0 and REG1 regulators work together.
#ifndef SAFE_VDD_VOLTAGE_THRESHOLD
#define SAFE_VDD_VOLTAGE_THRESHOLD 2.7
#endif

// hysteresis value
#ifndef SAFE_VDD_VOLTAGE_THRESHOLD_HYST
#define SAFE_VDD_VOLTAGE_THRESHOLD_HYST 0.2
#endif

uint16_t getVDDVoltage();

// Weak empty variant shutdown prep function.
// May be redefined by variant files.
void variant_shutdown() __attribute__((weak));
void variant_shutdown() {}

// Optional variant hook called each nrf52Loop(); e.g. for low-VDD System OFF.
void variant_nrf52LoopHook(void) __attribute__((weak));
void variant_nrf52LoopHook(void) {}

// Return false to skip LPCOMP wake when entering System OFF (e.g. user CLI shutdown).
// noinline: weak default and call site are in this file; without it GCC may inline the
// weak body and never link the strong override from variant.cpp.
__attribute__((noinline)) bool variant_enableBatteryLpcompWake() __attribute__((weak));
__attribute__((noinline)) bool variant_enableBatteryLpcompWake()
{
    return true;
}

static nrfx_wdt_t nrfx_wdt = NRFX_WDT_INSTANCE(0);
static nrfx_wdt_channel_id nrfx_wdt_channel_id_nrf52_main;

// This is a public global so that the debugger can set it to false automatically from our gdbinit
// @phaseloop comment: most part of codebase, including filesystem flash driver depend on softdevice
// methods so disabling it may actually crash thing. Proceed with caution.

bool useSoftDevice = true; // Set to false for easier debugging

static inline void debugger_break(void)
{
    __asm volatile("bkpt #0x01\n\t"
                   "mov pc, lr\n\t");
}

// PowerHAL NRF52 specific function implementations
bool powerHAL_isVBUSConnected()
{
    return NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk;
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
    // First thing in the earliest boot hook, so the whole of boot (GPS, screen,
    // I2C scan, radio init) runs on the buck converters, including builds where
    // Bluetooth never comes up. No-op unless the board opts in.
    nrf52EnableDCDC();

    // Enable POF power failure comparator. It will prevent writing to NVMC flash when supply voltage is too low.
    // Set to some low value as last resort - powerHAL_isPowerLevelSafe uses different method and should manage proper node
    // behaviour on its own.

    // POFWARN is pretty useless for node power management because it triggers only once and clearing this event will not
    // re-trigger it again until voltage rises to safe level and drops again. So we will use SAADC routed to VDD to read safely
    // voltage.

    // @phaseloop: I disable POFCON for now because it seems to be unreliable or buggy. Even when set at 2.0V it
    // triggers below 2.8V and corrupts data when pairing bluetooth - because it prevents filesystem writes and
    // adafruit BLE library triggers lfs_assert which reboots node and formats filesystem.
    // I did experiments with bench power supply and no matter what is set to POFCON, it always triggers right below
    // 2.8V. I compared raw registry values with datasheet.

    NRF_POWER->POFCON =
        ((POWER_POFCON_THRESHOLD_V22 << POWER_POFCON_THRESHOLD_Pos) | (POWER_POFCON_POF_Enabled << POWER_POFCON_POF_Pos));

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

    // VDD range on NRF52840 is 1.8-3.3V so we need to remap analog reference to 3.6V
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
    // turn off sleep only while connected via USB
    // return true;
    return !Serial; // the bool operator on the nrf52 serial class returns true if connected to a PC currently
    // return !(TinyUSBDevice.mounted() && !TinyUSBDevice.suspended());
}

// handle standard gcc assert failures
void __attribute__((noreturn)) __assert_func(const char *file, int line, const char *func, const char *failedexpr)
{
    LOG_ERROR("assert failed %s: %d, %s, test=%s", file, line, func, failedexpr);
    // debugger_break(); FIXME doesn't work, possibly not for segger
    // Reboot cpu
    NVIC_SystemReset();
}

#if defined(NRF52_USE_DCDC_REG0) || defined(NRF52_USE_DCDC_REG1)
#define NRF52_DCDC_ENABLED
#endif

#ifdef NRF52_DCDC_ENABLED
#include <nrf_sdm.h>

// Enable the buck converters on boards with the DC/DC inductors fitted, replacing
// the LDOs and cutting active/radio current. REG1 is the main stage (VDD -> core);
// REG0 is the high-voltage stage and only does anything when the part is supplied
// through VDDH. Each has its own flag because a board may fit inductors for one,
// the other, or both.
//
// Enablement runs from powerHAL_platformInit(), which is the earliest hook in boot
// and well before consoleInit() -- so nothing logged from here would reach the
// console. Latch the outcome instead and let nrf52LogDCDCStatus() print it once the
// console exists. The POWER peripheral belongs to the SoftDevice once that is up,
// so pick the access method accordingly; at powerHAL time it never is, but the
// check keeps the helper correct wherever it is called from.
// reg0Err/reg1Err are only meaningful when viaSoftDevice is set: the direct-register
// path has no API status to report, only whether the register read back as set.
static struct {
    bool reg0Attempted, reg1Attempted;
    bool reg0Ok, reg1Ok;
    uint32_t reg0Err, reg1Err;
    bool viaSoftDevice;
    bool stateUnknown;
    uint32_t stateErr;
    bool highVoltageMode;
} dcdcStatus;

void nrf52EnableDCDC()
{
    uint8_t sdEnabled = 0;
    uint32_t stateErr = sd_softdevice_is_enabled(&sdEnabled);
    if (stateErr != NRF_SUCCESS) {
        // The API documents no failure mode, but if we ever cannot tell who owns the
        // POWER peripheral, do nothing rather than guess: writing DCDCEN directly
        // while the SoftDevice owns POWER is undefined, and staying on the LDO costs
        // efficiency but nothing else. nrf52LogDCDCStatus() reports this.
        dcdcStatus.stateUnknown = true;
        dcdcStatus.stateErr = stateErr;
        return;
    }

    dcdcStatus.viaSoftDevice = sdEnabled;
    dcdcStatus.highVoltageMode = (NRF_POWER->MAINREGSTATUS & POWER_MAINREGSTATUS_MAINREGSTATUS_Msk) ==
                                 (POWER_MAINREGSTATUS_MAINREGSTATUS_High << POWER_MAINREGSTATUS_MAINREGSTATUS_Pos);

    // REG0 first: it is the upstream stage when both are in use.
#ifdef NRF52_USE_DCDC_REG0
    dcdcStatus.reg0Attempted = true;
    if (sdEnabled) {
        dcdcStatus.reg0Err = sd_power_dcdc0_mode_set(NRF_POWER_DCDC_ENABLE);
        dcdcStatus.reg0Ok = (dcdcStatus.reg0Err == NRF_SUCCESS);
    } else {
        NRF_POWER->DCDCEN0 = 1;
        // Pre-SoftDevice the register is ours to read, so confirm the write stuck
        // rather than assuming it did.
        dcdcStatus.reg0Ok = NRF_POWER->DCDCEN0 != 0;
    }
#endif

#ifdef NRF52_USE_DCDC_REG1
    dcdcStatus.reg1Attempted = true;
    if (sdEnabled) {
        dcdcStatus.reg1Err = sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
        dcdcStatus.reg1Ok = (dcdcStatus.reg1Err == NRF_SUCCESS);
    } else {
        NRF_POWER->DCDCEN = 1;
        dcdcStatus.reg1Ok = NRF_POWER->DCDCEN != 0;
    }
#endif
}

// Report what the early enablement actually achieved. Called from nrf52Setup(),
// which runs after consoleInit(), so this is the first point the result can be seen.
void nrf52LogDCDCStatus()
{
    if (dcdcStatus.stateUnknown) {
        LOG_ERROR("DCDC: SoftDevice state unreadable (err=%lu); enable skipped, still on LDO",
                  (unsigned long)dcdcStatus.stateErr);
        return;
    }

#ifdef NRF52_USE_DCDC_REG0
    if (dcdcStatus.reg0Attempted) {
        if (!dcdcStatus.reg0Ok && dcdcStatus.viaSoftDevice)
            LOG_ERROR("DCDC: REG0 enable FAILED (err=%lu), still on LDO", (unsigned long)dcdcStatus.reg0Err);
        else if (!dcdcStatus.reg0Ok)
            LOG_ERROR("DCDC: REG0 enable FAILED, DCDCEN0 read back 0, still on LDO");
        else if (dcdcStatus.highVoltageMode)
            LOG_INFO("DCDC: REG0 buck enabled");
        else
            // Not an error: the flag is set but VDDH is not the supply, so REG0 is bypassed.
            LOG_WARN("DCDC: REG0 buck enabled but part is not in high-voltage mode; no effect");
    }
#endif

#ifdef NRF52_USE_DCDC_REG1
    if (dcdcStatus.reg1Attempted) {
        if (dcdcStatus.reg1Ok)
            LOG_INFO("DCDC: REG1 buck enabled");
        else if (dcdcStatus.viaSoftDevice)
            LOG_ERROR("DCDC: REG1 enable FAILED (err=%lu), still on LDO", (unsigned long)dcdcStatus.reg1Err);
        else
            LOG_ERROR("DCDC: REG1 enable FAILED, DCDCEN read back 0, still on LDO");
    }
#endif
}

// Re-assert under the SoftDevice once it owns POWER. The mode persists across
// SoftDevice enablement, but this is the unambiguously supported call in that
// regime and it is idempotent.
void nrf52ReassertDCDC()
{
#ifdef NRF52_USE_DCDC_REG0
    uint32_t err0 = sd_power_dcdc0_mode_set(NRF_POWER_DCDC_ENABLE);
    if (err0 != NRF_SUCCESS)
        LOG_ERROR("DCDC: REG0 re-assert failed, err=%lu", (unsigned long)err0);
#endif
#ifdef NRF52_USE_DCDC_REG1
    uint32_t err1 = sd_power_dcdc_mode_set(NRF_POWER_DCDC_ENABLE);
    if (err1 != NRF_SUCCESS)
        LOG_ERROR("DCDC: REG1 re-assert failed, err=%lu", (unsigned long)err1);
#endif
}
#else
void nrf52EnableDCDC() {}
void nrf52LogDCDCStatus() {}
void nrf52ReassertDCDC() {}
#endif

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
    uint64_t device_id_start = ((uint64_t)NRF_FICR->DEVICEID[1] << 32) | NRF_FICR->DEVICEID[0];
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
static unsigned long millis_until_formatting_again = 0;

// Report the critical error from loop(), giving a chance for the screen to be initialized first.
inline void reportLittleFSCorruptionOnce()
{
    static bool report_corruption = !!millis_until_formatting_again;
    if (report_corruption) {
        report_corruption = false;
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_FLASH_CORRUPTION_UNRECOVERABLE);
    }
}
} // namespace

void preFSBegin()
{
    // The GPREGRET register keeps its value across warm boots. Check that this is a warm boot and, if GPREGRET
    // is set to NRF52_MAGIC_LFS_IS_CORRUPT, format LittleFS.
    if (!(NRF_POWER->RESETREAS == 0 && NRF_POWER->GPREGRET == NRF52_MAGIC_LFS_IS_CORRUPT))
        return;
    NRF_POWER->GPREGRET = 0;
    millis_until_formatting_again = millis() + MULTIPLE_CORRUPTION_DELAY_MILLIS;
    InternalFS.format();
    LOG_INFO("LittleFS format complete; restoring default settings");
}

extern "C" void lfs_assert(const char *reason)
{
    LOG_ERROR("LittleFS corruption detected: %s", reason);
    if (millis_until_formatting_again > millis()) {
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_FLASH_CORRUPTION_UNRECOVERABLE);
        const long millis_remain = millis_until_formatting_again - millis();
        LOG_WARN("Pausing %d seconds to avoid wear on flash storage", millis_remain / 1000);
        delay(millis_remain);
    }
    LOG_INFO("Rebooting to format LittleFS");
    delay(500); // Give the serial port a bit of time to output that last message.
    // Try setting GPREGRET with the SoftDevice first. If that fails (perhaps because the SD hasn't been initialize yet) then set
    // NRF_POWER->GPREGRET directly.

    // TODO: this will/can crash CPU if bluetooth stack is not compiled in or bluetooth is not initialized
    // (regardless if enabled or disabled) - as there is no live SoftDevice stack
    // implement "safe" functions detecting softdevice stack state and using proper method to set registers

    // do not set GPREGRET if POFWARN is triggered because it means lfs_assert reports flash undervoltage protection
    // and not data corruption. Reboot is fine as boot procedure will wait until power level is safe again

    if (!NRF_POWER->EVENTS_POFWARN) {
        if (!(sd_power_gpregret_clr(0, 0xFF) == NRF_SUCCESS &&
              sd_power_gpregret_set(0, NRF52_MAGIC_LFS_IS_CORRUPT) == NRF_SUCCESS)) {
            NRF_POWER->GPREGRET = NRF52_MAGIC_LFS_IS_CORRUPT;
        }
    }

    // TODO: this should not be done when SoftDevice is enabled as device will not boot back on soft reset
    // as some data is retained in RAM which will prevent re-enabling bluetooth stack
    // Google what Nordic has to say about NVIC_* + SoftDevice
    NVIC_SystemReset();
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
    nrfx_wdt_channel_feed(&nrfx_wdt, nrfx_wdt_channel_id_nrf52_main);

    checkSDEvents();
    reportLittleFSCorruptionOnce();

    variant_nrf52LoopHook(); // Optional variant hook called each nrf52Loop();
}

#ifdef USE_SEMIHOSTING
#include <SemihostingStream.h>
#include <meshUtils.h>

/**
 * Note: this variable is in BSS and therfore false by default.  But the gdbinit
 * file will be installing a temporary breakpoint that changes wantSemihost to true.
 */
bool wantSemihost;

/**
 * Turn on semihosting if the ICE debugger wants it.
 */
void nrf52InitSemiHosting()
{
    if (wantSemihost) {
        static SemihostingStream semiStream;
        // We must dynamically alloc because the constructor does semihost operations which
        // would crash any load not talking to a debugger
        semiStream.open();
        semiStream.println("Semihosting starts!");
        // Redirect our serial output to instead go via the ICE port
        console->setDestination(&semiStream);
    }
}
#endif

void nrf52Setup()
{
    // The buck converters were enabled back in powerHAL_platformInit(), before the
    // console existed. This is the first point their status can actually be printed.
    nrf52LogDCDCStatus();

#ifdef ADC_V
    pinMode(ADC_V, INPUT);
#endif

    // The Adafruit core's init() (cores/nRF5/wiring.c) caches RESETREAS into a static and then
    // W1C-clears the hardware register before setup() ever runs, so a raw NRF_POWER->RESETREAS
    // read here is ALWAYS 0. Use the core's cached copy so this log line is actually meaningful
    // (0x1 pin reset, 0x2 watchdog, 0x4 soft reset/SREQ, 0x8 CPU lockup, 0x10000 System OFF wake).
    uint32_t why = readResetReason();
    // per
    // https://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.nrf52832.ps.v1.1%2Fpower.html
    LOG_DEBUG("Reset reason: 0x%x", why);

#ifdef USE_SEMIHOSTING
    nrf52InitSemiHosting();
#endif

    // Per
    // https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/monitor-mode-debugging-with-j-link-and-gdbeclipse
    // This is the recommended setting for Monitor Mode Debugging
    NVIC_SetPriority(DebugMonitor_IRQn, 6UL);

#ifdef BQ25703A_ADDR
    auto *bq = new BQ25713();
    if (!bq->setup())
        LOG_ERROR("ERROR! Charge controller init failed");
#endif

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
    nrfx_wdt_config_t wdt0_config = {
        .behaviour = NRF_WDT_BEHAVIOUR_PAUSE_SLEEP_HALT, .reload_value = APP_WATCHDOG_SECS * 1000,
        // Note: Not using wdt interrupts.
        // .interrupt_priority = NRFX_WDT_DEFAULT_CONFIG_IRQ_PRIORITY
    };
    nrfx_err_t r = nrfx_wdt_init(&nrfx_wdt, &wdt0_config,
                                 nullptr // Watchdog event handler, not used, we just reset.
    );
    assert(r == NRFX_SUCCESS);

    r = nrfx_wdt_channel_alloc(&nrfx_wdt, &nrfx_wdt_channel_id_nrf52_main);
    assert(r == NRFX_SUCCESS);
}

void cpuDeepSleep(uint32_t msecToWake)
{
    // FIXME, configure RTC or button press to wake us
    // FIXME, power down SPI, I2C, RAMs
#if HAS_WIRE
    Wire.end();
#endif
    SPI.end();
#if SPI_INTERFACES_COUNT > 1
    SPI1.end();
#endif
    if (Serial)       // Another check in case of disabled default serial, does nothing bad
        Serial.end(); // This may cause crashes as debug messages continue to flow.

        // This causes troubles with waking up on nrf52 (on pro-micro in particular):
        // we have no Serial1 in use on nrf52, check Serial and GPS modules.
#ifdef PIN_SERIAL1_RX
    if (Serial1) // A straightforward solution to the wake from deepsleep problem
        Serial1.end();
#endif

    setBluetoothEnable(false);

#ifdef RAK4630
#ifdef PIN_3V3_EN
    digitalWrite(PIN_3V3_EN, LOW);
#endif
#ifdef AQ_SET_PIN
    // RAK-12039 set pin for Air quality sensor
    digitalWrite(AQ_SET_PIN, LOW);
#endif
#endif
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
        // Resume on user button press
        // https://github.com/lyusupov/SoftRF/blob/81c519ca75693b696752235d559e881f2e0511ee/software/firmware/source/SoftRF/src/platform/nRF52.cpp#L1738
        constexpr uint32_t DFU_MAGIC_SKIP = 0x6d;
        sd_power_gpregret_clr(0, 0xFF);           // Clear the register before setting a new values in it for stability reasons
        sd_power_gpregret_set(0, DFU_MAGIC_SKIP); // Equivalent NRF_POWER->GPREGRET = DFU_MAGIC_SKIP

        // FIXME, use system off mode with ram retention for key state?
        // FIXME, use non-init RAM per
        // https://devzone.nordicsemi.com/f/nordic-q-a/48919/ram-retention-settings-with-softdevice-enabled

#ifdef BATTERY_LPCOMP_INPUT
        // Only enable LPCOMP wake if the variant allows it
        if (variant_enableBatteryLpcompWake()) {
            // Wake up if power rises again
            nrf_lpcomp_config_t c;
            c.reference = BATTERY_LPCOMP_THRESHOLD;
            c.detection = NRF_LPCOMP_DETECT_UP;
            c.hyst = NRF_LPCOMP_HYST_NOHYST;
            nrf_lpcomp_configure(NRF_LPCOMP, &c);
            nrf_lpcomp_input_select(NRF_LPCOMP, BATTERY_LPCOMP_INPUT);
            nrf_lpcomp_enable(NRF_LPCOMP);

            battery_adcEnable();

            nrf_lpcomp_task_trigger(NRF_LPCOMP, NRF_LPCOMP_TASK_START);
            while (!nrf_lpcomp_event_check(NRF_LPCOMP, NRF_LPCOMP_EVENT_READY))
                ;
        }
#endif

        auto ok = sd_power_system_off();
        if (ok != NRF_SUCCESS) {
            LOG_ERROR("FIXME: Ignoring soft device (EasyDMA pending?) and forcing system-off!");
            NRF_POWER->SYSTEMOFF = 1;
        }
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
// SDK kit does not have native USB like almost all other NRF52 boards
#ifdef NRF_USE_SERIAL_DFU
    enterSerialDfu();
#else
    enterUf2Dfu();
#endif
}
