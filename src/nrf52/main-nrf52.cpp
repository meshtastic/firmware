#include "NRF52Bluetooth.h"
#include "configuration.h"
#include "graphics/TFTDisplay.h"
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
static const bool enableBle = false; // Set to false for easier debugging

void setBluetoothEnable(bool on)
{
    if (on != bleOn) {
        if (on) {
            if (!nrf52Bluetooth) {
                if (!enableBle)
                    DEBUG_MSG("DISABLING NRF52 BLUETOOTH WHILE DEBUGGING\n");
                else {
                    nrf52Bluetooth = new NRF52Bluetooth();
                    nrf52Bluetooth->setup();
                }
            }
        } else {
            if(nrf52Bluetooth)
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
    if(!bq->setup())
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
}

void cpuDeepSleep(uint64_t msecToWake)
{
  DEBUG_MSG("FIXME: implement NRF52 deep sleep enter actions\n");
  // FIXME, configure RTC to wake us
  // FIXME, power down SPI, I2C, RAMs

  // FIXME, use system off mode with ram retention for key state?
  // FIXME, use non-init RAM per https://devzone.nordicsemi.com/f/nordic-q-a/48919/ram-retention-settings-with-softdevice-enabled

  while(1) {
    delay(5000);
    DEBUG_MSG(".");
  }

  // FIXME, after wake power up SPI, I2C, RAMs, reinit LORA
  DEBUG_MSG("FIXME: implement NRF52 deep sleep wake actions\n");
}