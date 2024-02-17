#include "configuration.h"
#include <Adafruit_TinyUSB.h>
#include <Adafruit_nRFCrypto.h>
#include <SPI.h>
#include <Wire.h>
#include <assert.h>
#include <ble_gap.h>
#include <memory.h>
#include <stdio.h>
// #include <Adafruit_USBD_Device.h>
#include "NodeDB.h"
#include "error.h"
#include "main.h"

#ifdef BQ25703A_ADDR
#include "BQ25713.h"
#endif

static inline void debugger_break(void)
{
    __asm volatile("bkpt #0x01\n\t"
                   "mov pc, lr\n\t");
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
    LOG_ERROR("assert failed %s: %d, %s, test=%s\n", file, line, func, failedexpr);
    // debugger_break(); FIXME doesn't work, possibly not for segger
    // Reboot cpu
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

static void initBrownout()
{
    auto vccthresh = POWER_POFCON_THRESHOLD_V24;

    auto err_code = sd_power_pof_enable(POWER_POFCON_POF_Enabled);
    assert(err_code == NRF_SUCCESS);

    err_code = sd_power_pof_threshold_set(vccthresh);
    assert(err_code == NRF_SUCCESS);

    // We don't bother with setting up brownout if soft device is disabled - because during production we always use softdevice
}

static bool bleOn = false;
static const bool useSoftDevice = true; // Set to false for easier debugging

void setBluetoothEnable(bool on)
{
    if (on != bleOn && config.bluetooth.enabled == true) {
        if (on) {
            if (!nrf52Bluetooth) {
                if (!useSoftDevice)
                    LOG_INFO("DISABLING NRF52 BLUETOOTH WHILE DEBUGGING\n");
                else {
                    nrf52Bluetooth = new NRF52Bluetooth();
                    nrf52Bluetooth->setup();

                    // We delay brownout init until after BLE because BLE starts soft device
                    initBrownout();
                }
            }
        } else if (nrf52Bluetooth) {
            nrf52Bluetooth->shutdown();
        }
        bleOn = on;
    }
}

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
                LOG_DEBUG("Unexpected SDevt %d\n", evt);
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
    checkSDEvents();
}

void nrf52Setup()
{
    auto why = NRF_POWER->RESETREAS;
    // per
    // https://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.nrf52832.ps.v1.1%2Fpower.html
    LOG_DEBUG("Reset reason: 0x%x\n", why);

    // Per
    // https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/monitor-mode-debugging-with-j-link-and-gdbeclipse
    // This is the recommended setting for Monitor Mode Debugging
    NVIC_SetPriority(DebugMonitor_IRQn, 6UL);

#ifdef BQ25703A_ADDR
    auto *bq = new BQ25713();
    if (!bq->setup())
        LOG_ERROR("ERROR! Charge controller init failed\n");
#endif

    // Init random seed
    union seedParts {
        uint32_t seed32;
        uint8_t seed8[4];
    } seed;
    nRFCrypto.begin();
    nRFCrypto.Random.generate(seed.seed8, sizeof(seed.seed8));
    LOG_DEBUG("Setting random seed %u\n", seed.seed32);
    randomSeed(seed.seed32);
    nRFCrypto.end();
}

void cpuDeepSleep(uint32_t msecToWake)
{
    // FIXME, configure RTC or button press to wake us
    // FIXME, power down SPI, I2C, RAMs
#if HAS_WIRE
    Wire.end();
#endif
    SPI.end();
    // This may cause crashes as debug messages continue to flow.
    Serial.end();

#ifdef PIN_SERIAL_RX1
    Serial1.end();
#endif
    setBluetoothEnable(false);

#ifdef RAK4630
#ifdef PIN_3V3_EN
    digitalWrite(PIN_3V3_EN, LOW);
#endif
#ifndef USE_EINK
    // RAK-12039 set pin for Air quality sensor
    digitalWrite(AQ_SET_PIN, LOW);
#endif
#endif
    // Sleepy trackers or sensors can low power "sleep"
    // Don't enter this if we're sleeping portMAX_DELAY, since that's a shutdown event
    if (msecToWake != portMAX_DELAY &&
        (config.device.role == meshtastic_Config_DeviceConfig_Role_TRACKER ||
         config.device.role == meshtastic_Config_DeviceConfig_Role_TAK_TRACKER ||
         config.device.role == meshtastic_Config_DeviceConfig_Role_SENSOR) &&
        config.power.is_power_saving == true) {
        sd_power_mode_set(NRF_POWER_MODE_LOWPWR);
        delay(msecToWake);
        NVIC_SystemReset();
    } else {
        // FIXME, use system off mode with ram retention for key state?
        // FIXME, use non-init RAM per
        // https://devzone.nordicsemi.com/f/nordic-q-a/48919/ram-retention-settings-with-softdevice-enabled
        auto ok = sd_power_system_off();
        if (ok != NRF_SUCCESS) {
            LOG_ERROR("FIXME: Ignoring soft device (EasyDMA pending?) and forcing system-off!\n");
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
    enterUf2Dfu();
}