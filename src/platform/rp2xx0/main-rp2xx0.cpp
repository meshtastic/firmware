#include "configuration.h"
#include "hardware/xosc.h"
#include <hardware/clocks.h>
#include <hardware/pll.h>
#include <pico/stdlib.h>
#include <pico/unique_id.h>

#ifdef __PLAT_RP2040__
#include <pico/sleep.h>

static bool awake;

static void sleep_callback(void)
{
    awake = true;
}

void epoch_to_datetime(time_t epoch, datetime_t *dt)
{
    struct tm *tm_info;

    tm_info = gmtime(&epoch);
    dt->year = tm_info->tm_year;
    dt->month = tm_info->tm_mon + 1;
    dt->day = tm_info->tm_mday;
    dt->dotw = tm_info->tm_wday;
    dt->hour = tm_info->tm_hour;
    dt->min = tm_info->tm_min;
    dt->sec = tm_info->tm_sec;
}

void debug_date(datetime_t t)
{
    LOG_DEBUG("%d %d %d %d %d %d %d", t.year, t.month, t.day, t.hour, t.min, t.sec, t.dotw);
    uart_default_tx_wait_blocking();
}

void cpuDeepSleep(uint32_t msecs)
{

    time_t seconds = (time_t)(msecs / 1000);
    datetime_t t_init, t_alarm;

    awake = false;
    // Start the RTC
    rtc_init();
    epoch_to_datetime(0, &t_init);
    rtc_set_datetime(&t_init);
    epoch_to_datetime(seconds, &t_alarm);
    // debug_date(t_init);
    // debug_date(t_alarm);
    uart_default_tx_wait_blocking();
    sleep_run_from_dormant_source(DORMANT_SOURCE_ROSC);
    sleep_goto_sleep_until(&t_alarm, &sleep_callback);

    // Make sure we don't wake
    while (!awake) {
        delay(1);
    }

    /* For now, I don't know how to revert this state
        We just reboot in order to get back operational */
    rp2040.reboot();

    /* Set RP2040 in dormant mode. Will not wake up. */
    // xosc_dormant();
}

#else
void cpuDeepSleep(uint32_t msecs)
{
    /* Set RP2040 in dormant mode. Will not wake up. */
    xosc_dormant();
}
#endif

void setBluetoothEnable(bool enable)
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

    LOG_INFO("Clock speed:");
    LOG_INFO("pll_sys  = %dkHz", f_pll_sys);
    LOG_INFO("pll_usb  = %dkHz", f_pll_usb);
    LOG_INFO("rosc     = %dkHz", f_rosc);
    LOG_INFO("clk_sys  = %dkHz", f_clk_sys);
    LOG_INFO("clk_peri = %dkHz", f_clk_peri);
    LOG_INFO("clk_usb  = %dkHz", f_clk_usb);
    LOG_INFO("clk_adc  = %dkHz", f_clk_adc);
    LOG_INFO("clk_rtc  = %dkHz", f_clk_rtc);
#endif
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