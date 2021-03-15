#include "NRF52Bluetooth.h"
#include "configuration.h"
#include "error.h"
#include <SPI.h>
#include <Wire.h>
#include <assert.h>
#include <ble_gap.h>
#include <memory.h>
#include <stdio.h>

#ifdef NRF52840_XXAA
// #include <nrf52840.h>
#endif

// #define USE_SOFTDEVICE

static inline void debugger_break(void)
{
    __asm volatile("bkpt #0x01\n\t"
                   "mov pc, lr\n\t");
}

// handle standard gcc assert failures
void __attribute__((noreturn)) __assert_func(const char *file, int line, const char *func, const char *failedexpr)
{
    DEBUG_MSG("assert failed %s: %d, %s, test=%s\n", file, line, func, failedexpr);
    // debugger_break(); FIXME doesn't work, possibly not for segger
    while (1)
        ; // FIXME, reboot!
}

void getMacAddr(uint8_t *dmac)
{
    ble_gap_addr_t addr;

#ifdef USE_SOFTDEVICE
    uint32_t res = sd_ble_gap_addr_get(&addr);
    assert(res == NRF_SUCCESS);
    memcpy(dmac, addr.addr, 6);
#else
    const uint8_t *src = (const uint8_t *)NRF_FICR->DEVICEADDR;
    dmac[5] = src[0];
    dmac[4] = src[1];
    dmac[3] = src[2];
    dmac[2] = src[3];
    dmac[1] = src[4];
    dmac[0] = src[5] | 0xc0; // MSB high two bits get set elsewhere in the bluetooth stack
#endif
}

NRF52Bluetooth *nrf52Bluetooth;

static bool bleOn = false;
static const bool useSoftDevice = false; // Set to false for easier debugging

void setBluetoothEnable(bool on)
{
    if (on != bleOn) {
        if (on) {
            if (!nrf52Bluetooth) {
                if (!useSoftDevice)
                    DEBUG_MSG("DISABLING NRF52 BLUETOOTH WHILE DEBUGGING\n");
                else {
                    nrf52Bluetooth = new NRF52Bluetooth();
                    nrf52Bluetooth->setup();
                }
            }
        } else {
            if (nrf52Bluetooth)
                nrf52Bluetooth->shutdown();
        }
        bleOn = on;
    }
}

/**
 * Override printf to use the SEGGER output library
 */
int printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    auto res = SEGGER_RTT_vprintf(0, fmt, &args);
    va_end(args);
    return res;
}

#include "BQ25713.h"

void initBrownout()
{
    auto vccthresh = POWER_POFCON_THRESHOLD_V28;
    auto vcchthresh = POWER_POFCON_THRESHOLDVDDH_V27;

    if (useSoftDevice) {
        auto err_code = sd_power_pof_enable(POWER_POFCON_POF_Enabled);
        assert(err_code == NRF_SUCCESS);

        err_code = sd_power_pof_threshold_set(vccthresh);
        assert(err_code == NRF_SUCCESS);
    }
    else {
        NRF_POWER->POFCON = POWER_POFCON_POF_Msk | (vccthresh << POWER_POFCON_THRESHOLD_Pos) | (vcchthresh << POWER_POFCON_THRESHOLDVDDH_Pos);
    }
}

void checkSDEvents()
{
    if (useSoftDevice) {
        uint32_t evt;
        while (NRF_ERROR_NOT_FOUND == sd_evt_get(&evt)) {
            switch (evt) {
            case NRF_EVT_POWER_FAILURE_WARNING:
                recordCriticalError(CriticalErrorCode_Brownout);
                break;

            default:
                DEBUG_MSG("Unexpected SDevt %d\n", evt);
                break;
            }
        }
    } else {
        if(NRF_POWER->EVENTS_POFWARN)
            recordCriticalError(CriticalErrorCode_Brownout);
    }
}

void nrf52Loop()
{
    checkSDEvents();
}

void nrf52Setup()
{

    auto why = NRF_POWER->RESETREAS;
    // per https://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.nrf52832.ps.v1.1%2Fpower.html
    DEBUG_MSG("Reset reason: 0x%x\n", why);

    // Per https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/monitor-mode-debugging-with-j-link-and-gdbeclipse
    // This is the recommended setting for Monitor Mode Debugging
    NVIC_SetPriority(DebugMonitor_IRQn, 6UL);

#ifdef BQ25703A_ADDR
    auto *bq = new BQ25713();
    if (!bq->setup())
        DEBUG_MSG("ERROR! Charge controller init failed\n");
#endif

    // Init random seed
    // FIXME - use this to get random numbers
    // #include "nrf_rng.h"
    // uint32_t r;
    // ble_controller_rand_vector_get_blocking(&r, sizeof(r));
    // randomSeed(r);
    DEBUG_MSG("FIXME, call randomSeed\n");
    // ::printf("TESTING PRINTF\n");

    initBrownout();
}

void cpuDeepSleep(uint64_t msecToWake)
{
    // FIXME, configure RTC or button press to wake us
    // FIXME, power down SPI, I2C, RAMs
  #if WIRE_INTERFACES_COUNT > 0
    Wire.end();
  #endif
    SPI.end();
    Serial.end();
    
  #ifdef PIN_SERIAL_RX1
    Serial1.end();
  #endif

    // FIXME, use system off mode with ram retention for key state?
    // FIXME, use non-init RAM per
    // https://devzone.nordicsemi.com/f/nordic-q-a/48919/ram-retention-settings-with-softdevice-enabled

    auto ok = sd_power_system_off();
    if (ok != NRF_SUCCESS) {
        DEBUG_MSG("FIXME: Ignoring soft device (EasyDMA pending?) and forcing system-off!\n");
        NRF_POWER->SYSTEMOFF = 1;
    }

    // The following code should not be run, because we are off
    while (1) {
        delay(5000);
        DEBUG_MSG(".");
    }
}