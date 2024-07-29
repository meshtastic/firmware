#include "configuration.h"
#include <hardware/clocks.h>
#include <hardware/pll.h>
#include <pico/stdlib.h>
#include <pico/unique_id.h>
#include <stdio.h>

void setBluetoothEnable(bool enable)
{
    // not needed
}

// loop code specific to RP2040 targets
void rp2040Loop()
{
    // Watchdog can not be enabled on startup since some initializtion wich takes place
    // after, especially the wifi initialization, takes a long time and and may exceed
    // the maximum delay of the hardare watchdog. So we start the watchdog after having
    // called the loop a few times since the first calls to loop() is still interupted
    // by long running, blocking calls.
    //
    // This leaves a small window where the device can get stuck
    // in the boot phase, and never be rebooted, but it's a heck
    // better than having no watchdog at all ;)
    //
    // Check if the watchdog is enabled, and if not, then start it
    static bool watchdog_is_enabled = false;
    static int watchdog_enable_delay = 3;
    static uint32_t last_reset_ms = to_ms_since_boot(get_absolute_time());
    static int watchdog_reset_cnt = 0;
    if (!watchdog_is_enabled) {
        if (watchdog_enable_delay > 0) {
            watchdog_enable_delay--;
            LOG_INFO("rp2040Loop(): Delaying enable of the rp2040 hardware watchdog: %d retries left\n", watchdog_enable_delay);
        } else {
            // Enable the hardware watchdog, 8 seconds delay before reboot if not reset
            LOG_INFO("rp2040Loop(): Enabling rp2040 hardware watchdog\n");
            watchdog_enable(0x7fffff, true);
            watchdog_is_enabled = true;
        }
    } else {
        // Reset watchdog periodically, after aprox. 3 seconds.
        // This leaves plenty of time to perform long running radio operations before the
        // hardware delay of 8 seconds runs out.
        // Note: The uint32_t gives us aprox. 49 days of uptime before it rolls over, hence
        //       the need for checking if 'now' is before last reset also.
        uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_reset_ms > 3000 || now_ms < last_reset_ms) {
            LOG_TRACE("rp2040Loop(): watchdog reset at %ld seconds after boot (or wrap)\n", now_ms / 1000);
            watchdog_update();
            last_reset_ms = now_ms;
        }
    }
}

void cpuDeepSleep(uint32_t msecs)
{
    // not needed
}

void updateBatteryLevel(uint8_t level)
{
    // not needed
}

void getMacAddr(uint8_t *dmac)
{
    pico_unique_board_id_t src;
    pico_get_unique_board_id(&src);
    dmac[5] = src.id[7];
    dmac[4] = src.id[6];
    dmac[3] = src.id[5];
    dmac[2] = src.id[4];
    dmac[1] = src.id[3];
    dmac[0] = src.id[2];
}

void rp2040Setup()
{
    /* Sets a random seed to make sure we get different random numbers on each boot.
       Taken from CPU cycle counter and ROSC oscillator, so should be pretty random.
    */
    randomSeed(rp2040.hwrand32());

#ifdef RP2040_SLOW_CLOCK
    uint f_pll_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_SYS_CLKSRC_PRIMARY);
    uint f_pll_usb = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_PLL_USB_CLKSRC_PRIMARY);
    uint f_rosc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_ROSC_CLKSRC);
    uint f_clk_sys = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS);
    uint f_clk_peri = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_PERI);
    uint f_clk_usb = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_USB);
    uint f_clk_adc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_ADC);
    uint f_clk_rtc = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_RTC);

    LOG_INFO("Clock speed:\n");
    LOG_INFO("pll_sys  = %dkHz\n", f_pll_sys);
    LOG_INFO("pll_usb  = %dkHz\n", f_pll_usb);
    LOG_INFO("rosc     = %dkHz\n", f_rosc);
    LOG_INFO("clk_sys  = %dkHz\n", f_clk_sys);
    LOG_INFO("clk_peri = %dkHz\n", f_clk_peri);
    LOG_INFO("clk_usb  = %dkHz\n", f_clk_usb);
    LOG_INFO("clk_adc  = %dkHz\n", f_clk_adc);
    LOG_INFO("clk_rtc  = %dkHz\n", f_clk_rtc);
#endif

    // We have to delay the watchdog initialization since some parts
    // of the initialization can take longer than the maximum delay
    // of the hardware watchdog allows.
    LOG_DEBUG("rp2040Setup(): watchdog initialization delayed\n");
}

void enterDfuMode()
{
    reset_usb_boot(0, 0);
}

/* Init in early boot state. */
#ifdef RP2040_SLOW_CLOCK
void initVariant()
{
    /* Set the system frequency to 18 MHz. */
    set_sys_clock_khz(18 * KHZ, false);
    /* The previous line automatically detached clk_peri from clk_sys, and
       attached it to pll_usb. We need to attach clk_peri back to system PLL to keep SPI
       working at this low speed.
       For details see https://github.com/jgromes/RadioLib/discussions/938
    */
    clock_configure(clk_peri,
                    0,                                                // No glitchless mux
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, // System PLL on AUX mux
                    18 * MHZ,                                         // Input frequency
                    18 * MHZ                                          // Output (must be same as no divider)
    );
    /* Run also ADC on lower clk_sys. */
    clock_configure(clk_adc, 0, CLOCKS_CLK_ADC_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, 18 * MHZ, 18 * MHZ);
    /* Run RTC from XOSC since USB clock is off */
    clock_configure(clk_rtc, 0, CLOCKS_CLK_RTC_CTRL_AUXSRC_VALUE_XOSC_CLKSRC, 12 * MHZ, 47 * KHZ);
    /* Turn off USB PLL */
    pll_deinit(pll_usb);
}
#endif