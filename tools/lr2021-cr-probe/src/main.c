/*
 * Zephyr port of the LR2021 coding-rate probe. Everything radio-specific is in lr2021_probe.c;
 * this file only wires six functions to Zephyr's SPI and GPIO APIs.
 *
 * Pins come from the /zephyr,user devicetree node in the board overlay, so no custom binding is
 * needed. Chip select is driven by hand rather than by the SPI driver, so that NSS stays asserted
 * for exactly one command frame - the LR2021 latches a command on the rising edge of NSS.
 *
 * Build one role or the other:
 *   west build -b <board> .                 # RX, the default
 *   west build -b <board> . -- -DROLE=TX    # TX
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "lr2021_probe.h"

#define ZUSER DT_PATH(zephyr_user)

#ifndef PROBE_SPI_HZ
#define PROBE_SPI_HZ 8000000
#endif
/* Print the summary line every this many receptions. */
#ifndef PROBE_SUMMARY_EVERY
#define PROBE_SUMMARY_EVERY 20
#endif
/* 1 = the first time the reported rate differs from the configured one, write the configured code
 * back into the packet-status nibble and keep going. Off by default: observe first. */
#ifndef PROBE_AUTO_CLEAR
#define PROBE_AUTO_CLEAR 0
#endif
/* 1 = log every SPI frame, not just the init sequence. Very chatty under traffic. */
#ifndef PROBE_TRACE_ALL
#define PROBE_TRACE_ALL 0
#endif

static const struct device *spi_bus = DEVICE_DT_GET(DT_PHANDLE(ZUSER, spi_bus));
static const struct gpio_dt_spec cs_gpio = GPIO_DT_SPEC_GET_BY_IDX(ZUSER, cs_gpios, 0);
static const struct gpio_dt_spec busy_gpio = GPIO_DT_SPEC_GET_BY_IDX(ZUSER, busy_gpios, 0);
static const struct gpio_dt_spec reset_gpio = GPIO_DT_SPEC_GET_BY_IDX(ZUSER, reset_gpios, 0);

static const struct spi_config spi_cfg = {
    .frequency = PROBE_SPI_HZ,
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
    .cs = {0},
};

static void hal_spi_xfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    const struct spi_buf tx_buf = {.buf = (void *)tx, .len = len};
    const struct spi_buf rx_buf = {.buf = rx, .len = len};
    const struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};
    const struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

    gpio_pin_set_dt(&cs_gpio, 1); /* logical 1 asserts: the pin is declared active-low */
    int rc = spi_transceive(spi_bus, &spi_cfg, &tx_set, &rx_set);
    gpio_pin_set_dt(&cs_gpio, 0);

    if (rc != 0) {
        printk("! spi_transceive rc=%d\n", rc);
    }
}

static bool hal_busy_high(void)
{
    return gpio_pin_get_dt(&busy_gpio) == 1;
}

static void hal_reset_drive_low(bool low)
{
    gpio_pin_set_dt(&reset_gpio, low ? 1 : 0); /* declared active-low */
}

static void hal_delay_ms(uint32_t ms)
{
    k_msleep((int32_t)ms);
}

static uint32_t hal_millis(void)
{
    return (uint32_t)k_uptime_get_32();
}

static void hal_log_line(const char *line)
{
    printk("%s\n", line);
}

static const lr2021_hal_t probe_hal = {
    .spi_xfer = hal_spi_xfer,
    .busy_high = hal_busy_high,
    .reset_drive_low = hal_reset_drive_low,
    .delay_ms = hal_delay_ms,
    .millis = hal_millis,
    .log_line = hal_log_line,
};

static int pins_init(void)
{
    if (!device_is_ready(spi_bus)) {
        printk("! SPI bus not ready\n");
        return -1;
    }
    /* CS and NRESET idle de-asserted; both are declared active-low in the overlay */
    if (gpio_pin_configure_dt(&cs_gpio, GPIO_OUTPUT_INACTIVE) != 0 ||
        gpio_pin_configure_dt(&reset_gpio, GPIO_OUTPUT_INACTIVE) != 0 || gpio_pin_configure_dt(&busy_gpio, GPIO_INPUT) != 0) {
        printk("! GPIO configure failed\n");
        return -1;
    }
    return 0;
}

int main(void)
{
    k_msleep(500); /* let a USB console attach before the init trace goes out */

    if (pins_init() != 0) {
        return 0;
    }

#if PROBE_TRACE_ALL
    lr2021_probe_set_trace(true);
#endif

#if defined(PROBE_ROLE_TX)
    int rc = lr2021_probe_begin(&probe_hal, LR2021_ROLE_TX);
    if (rc != 0) {
        printk("! begin rc=%d\n", rc);
        return 0;
    }
    k_msleep(3000); /* give the receiver time to come up */
    for (;;) {
        rc = lr2021_probe_tx_sequence();
        if (rc != 0) {
            printk("! tx_sequence rc=%d\n", rc);
            return 0;
        }
        printk("# idle for 30 s, then the sequence runs again after a chip reset\n");
        k_msleep(30000);
        /* a fresh reset for each run, so every sequence starts from a clean receiver state when
         * both ends are power-cycled together */
        rc = lr2021_probe_begin(&probe_hal, LR2021_ROLE_TX);
        if (rc != 0) {
            printk("! re-begin rc=%d\n", rc);
            return 0;
        }
    }
#else
    int rc = lr2021_probe_begin(&probe_hal, LR2021_ROLE_RX);
    if (rc != 0) {
        printk("! begin rc=%d\n", rc);
        return 0;
    }

    uint32_t handled = 0;
    bool cleared_once = false;
    for (;;) {
        int got = lr2021_probe_rx_poll();
        if (got < 0) {
            printk("! rx_poll rc=%d\n", got);
            k_msleep(100);
            continue;
        }
        if (got == 0) {
            k_msleep(2);
            continue;
        }
        handled++;
        if (handled % PROBE_SUMMARY_EVERY == 0) {
            lr2021_probe_rx_summary();
        }
#if PROBE_AUTO_CLEAR
        if (!cleared_once && lr2021_probe_rx_last_cr() != lr2021_probe_configured_cr()) {
            (void)lr2021_probe_clear_cr_nibble();
            cleared_once = true;
        }
#else
        (void)cleared_once;
#endif
    }
#endif
    return 0;
}
