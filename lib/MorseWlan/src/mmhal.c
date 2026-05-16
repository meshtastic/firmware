#ifdef USE_MM_IOT_ESP32
/*
 * Copyright 2021-2023 Morse Micro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "mmhal.h"
#include "mmosal.h"
#include "mmutils.h"
#include "mmwlan.h"

#include "driver/gpio.h"
#include "esp_random.h"
#include "esp_system.h"
#include "sdkconfig.h"

void mmhal_init(void)
{
    /* We initialise the MM_RESET_N Pin here so that we can hold the MM6108 in reset regardless of
     * whether the mmhal_wlan_init/deinit function have been called. This allows us to ensure the
     * chip is in its lowest power state. You may want to revise this depending on your particular
     * hardware configuration. */
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1 << CONFIG_MM_RESET_N);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    gpio_set_level(CONFIG_MM_RESET_N, 0);

    /* Initialise the gpio ISR handler service. This allows per-pin GPIO interrupt handlers and is
     * what is used to register all the wlan related interrupt. */
    gpio_install_isr_service(0);
}

void mmhal_log_write(const uint8_t *data, size_t length)
{
    while (length--) {
        putc(*data++, stdout);
    }
}

void mmhal_log_flush(void) {}

void mmhal_read_mac_addr(uint8_t *mac_addr)
{
    /* We do not override the MAC address here. Therefore the driver will attempt to read it from
     * the chip and failing that will assign a randomly generated address. */
    (void)(mac_addr);
}

uint32_t mmhal_random_u32(uint32_t min, uint32_t max)
{
    /* Note: the below implementation does not guarantee a uniform distribution. */

    uint32_t random_value = esp_random();

    if (min == 0 && max == UINT32_MAX) {
        return random_value;
    } else {
        /* Calculate the range and shift required to fit within [min, max] */
        return (random_value % (max - min + 1)) + min;
    }
}

void mmhal_reset(void)
{
    esp_restart();
    while (1) {
    }
}

void mmhal_set_deep_sleep_veto(uint8_t veto_id)
{
    MM_UNUSED(veto_id);
}

void mmhal_clear_deep_sleep_veto(uint8_t veto_id)
{
    MM_UNUSED(veto_id);
}

void mmhal_set_led(uint8_t led, uint8_t level)
{
    MM_UNUSED(led);
    MM_UNUSED(level);
}

bool mmhal_get_hardware_version(char *version_buffer, size_t version_buffer_length)
{
    /* Note: You need to identify the correct hardware and or version
     *       here using whatever means available (GPIO's, version number stored in EEPROM, etc)
     *       and return the correct string here. */
    return !mmosal_safer_strcpy(version_buffer, "MM-ESP32S3 V1.0", version_buffer_length);
}

#endif /* USE_MM_IOT_ESP32 */
