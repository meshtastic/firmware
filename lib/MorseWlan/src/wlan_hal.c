#ifdef USE_MM_IOT_ESP32
/*
 * Copyright 2021-2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mmhal.h"
#include "mmosal.h"

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "driver/spi_master.h"
#include "esp_random.h"
#include "esp_system.h"

/** 10x8bit training seq */
#define BYTE_TRAIN 16

/** SPI hw interrupt handler. Must be set before enabling irq */
static mmhal_irq_handler_t spi_irq_handler = NULL;

/** busy interrupt handler. Must be set before enabling irq */
static mmhal_irq_handler_t busy_irq_handler = NULL;

static spi_device_handle_t spi_handle;

static void wlan_hal_gpio_init(void)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = ((1ull << CONFIG_MM_WAKE) | (1ull << CONFIG_MM_SPI_CS));
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    gpio_set_level(CONFIG_MM_WAKE, 0);
    gpio_set_level(CONFIG_MM_SPI_CS, 0);

    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ull << CONFIG_MM_BUSY);
    io_conf.pull_down_en = 1;
    gpio_config(&io_conf);

    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ull << CONFIG_MM_SPI_IRQ);
    io_conf.pull_down_en = 0;
    gpio_config(&io_conf);
}

static void wlan_hal_spi_init(void)
{
    esp_err_t ret;

    spi_bus_config_t buscfg = {
        .miso_io_num = CONFIG_MM_SPI_MISO,
        .mosi_io_num = CONFIG_MM_SPI_MOSI,
        .sclk_io_num = CONFIG_MM_SPI_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        /* max_transfer_sz defaults to 4092 if 0 when DMA enabled, or to SOC_SPI_MAXIMUM_BUFFER_SIZE
         * if DMA is disabled. */
        .max_transfer_sz = 0,
        .flags = SPICOMMON_BUSFLAG_MASTER,
    };
    ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        printf("spi_bus_initialize failed\n");
    }

    /* Selected the highest available SPI clock speed that is still below the MM6108's maximum of
     * 50MHz */
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = SPI_MASTER_FREQ_40M,
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    ret = spi_bus_add_device(SPI2_HOST, &dev_cfg, &spi_handle);
    if (ret != ESP_OK) {
        printf("spi_bus_add_device failed\n");
    }

    /* The actual clock frequency may not be the one that was set as it is re-calculated by the
     * driver to the nearest hardware-compatible number. Importantly it is the "nearest", so it could be above
     * the value set. */
    int actual_freq_khz = 0;
    spi_device_get_actual_freq(spi_handle, &actual_freq_khz);
    printf("Actual SPI CLK %dkHz\n", actual_freq_khz);
}

static void wlan_hal_spi_deinit(void)
{
    esp_err_t ret = spi_bus_remove_device(spi_handle);
    if (ret != ESP_OK) {
        printf("spi_bus_remove_device failed\n");
    }

    ret = spi_bus_free(SPI2_HOST);
    if (ret != ESP_OK) {
        printf("spi_bus_initialize failed\n");
    }
}

/**
 * Minium transfer length in bytes before interrupt based transactions are used. This is because
 * there is some setup time associated with using the interrupt based method when compared to the
 * polling method. In the cases where the difference in setup time exceeds the transaction duration
 * it is more efficient to uses the polling method instead of the interrupt based one. The below
 * equation was used to calculate this.
 *
 * (DMA_TRANSACTION_DURATION - POLL_TRANSACTION_DURATION) / (8/SPI_FREQ)
 *
 * The typical duration for the ESP32 can be found in the [transaction
 * duration](https://docs.espressif.com/projects/esp-idf/en/v5.1.1/esp32s3/api-reference/peripherals/spi_master.html#transaction-duration)
 * section of the docs.
 */
#define INTERRUPT_TRANSFER_MIN_LENGTH 75

static void spi_master_rw(const uint8_t *w_data, uint8_t *r_data, size_t len)
{
    spi_transaction_t trans_desc = {
        .rx_buffer = r_data,
        .tx_buffer = w_data,
        .length = (len * 8),
        .flags = 0,
    };

    esp_err_t err;
    if (len < INTERRUPT_TRANSFER_MIN_LENGTH) {
        err = spi_device_polling_transmit(spi_handle, &trans_desc);
    } else {
        err = spi_device_transmit(spi_handle, &trans_desc);
    }

    if (err != ESP_OK) {
        printf("SPI rw error = %x\n", err);
    }
}

void mmhal_wlan_hard_reset(void)
{
    gpio_set_level(CONFIG_MM_RESET_N, 0);
    mmosal_task_sleep(5);
    gpio_set_level(CONFIG_MM_RESET_N, 1);
    mmosal_task_sleep(20);
}

void mmhal_wlan_spi_cs_assert(void)
{
    gpio_set_level(CONFIG_MM_SPI_CS, 0);
}

void mmhal_wlan_spi_cs_deassert(void)
{
    gpio_set_level(CONFIG_MM_SPI_CS, 1);
}

uint8_t mmhal_wlan_spi_rw(uint8_t data)
{
    uint8_t readval;
    spi_master_rw(&data, &readval, 1);
    return readval;
}

void mmhal_wlan_spi_read_buf(uint8_t *buf, unsigned len)
{
    spi_master_rw(NULL, buf, len);
}

void mmhal_wlan_spi_write_buf(const uint8_t *buf, unsigned len)
{
    spi_master_rw(buf, NULL, len);
}

void mmhal_wlan_send_training_seq(void)
{
    mmhal_wlan_spi_cs_deassert();
    /* Send >74 clock pulses to card to stabilize CLK.
     * This method of stacking up the TX data is described in RM0090 rev 19 Figure 253.
     * It results is a reduction in the time between bytes of ~85% (316ns -> 48ns).
     * Could not get this to work for the other transactions however.
     */
    uint8_t buf[BYTE_TRAIN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    spi_master_rw(buf, NULL, BYTE_TRAIN);
}

void mmhal_wlan_register_spi_irq_handler(mmhal_irq_handler_t handler)
{
    spi_irq_handler = handler;
    gpio_isr_handler_add(CONFIG_MM_SPI_IRQ, (gpio_isr_t)spi_irq_handler, NULL);
}

bool mmhal_wlan_spi_irq_is_asserted(void)
{
    return !gpio_get_level(CONFIG_MM_SPI_IRQ);
}

void mmhal_wlan_set_spi_irq_enabled(bool enabled)
{
    if (enabled) {
        gpio_set_intr_type(CONFIG_MM_SPI_IRQ, GPIO_INTR_LOW_LEVEL);
    } else {
        gpio_set_intr_type(CONFIG_MM_SPI_IRQ, GPIO_INTR_DISABLE);
    }
}

void mmhal_wlan_init(void)
{
    wlan_hal_gpio_init();
    wlan_hal_spi_init();
    /* Raise the RESET_N line to enable the WLAN transceiver. */
    gpio_set_level(CONFIG_MM_RESET_N, 1);
}

void mmhal_wlan_deinit(void)
{
    /* Lower the RESET_N line to disable the WLAN transceiver. This will put the transceiver in its
     * lowest power state. */
    gpio_set_level(CONFIG_MM_RESET_N, 0);

    wlan_hal_spi_deinit();

    /* Clean up any ISR handlers that have been added. These will be added again if the WLAN
     * interface is brought back up. */
    gpio_isr_handler_remove(CONFIG_MM_SPI_IRQ);
    gpio_isr_handler_remove(CONFIG_MM_BUSY);
}

void mmhal_wlan_wake_assert(void)
{
    gpio_set_level(CONFIG_MM_WAKE, 1);
}

void mmhal_wlan_wake_deassert(void)
{
    gpio_set_level(CONFIG_MM_WAKE, 0);
}

bool mmhal_wlan_busy_is_asserted(void)
{
    return gpio_get_level(CONFIG_MM_BUSY);
}

void mmhal_wlan_register_busy_irq_handler(mmhal_irq_handler_t handler)
{
    busy_irq_handler = handler;
    gpio_isr_handler_add(CONFIG_MM_BUSY, (gpio_isr_t)busy_irq_handler, NULL);
}

void mmhal_wlan_set_busy_irq_enabled(bool enabled)
{
    if (enabled) {
        gpio_set_intr_type(CONFIG_MM_BUSY, GPIO_INTR_POSEDGE);
    } else {
        gpio_set_intr_type(CONFIG_MM_BUSY, GPIO_INTR_DISABLE);
    }
}

#endif /* USE_MM_IOT_ESP32 */
