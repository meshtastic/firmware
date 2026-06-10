#include "hyn_platform.h"

#include <Arduino.h>
#include <esp_log.h>

#include <TDeckMaxBoard.h>

#include "ExtensionIOXL9555.hpp"
#include "HynTouch.h"

namespace
{

constexpr const char *kTag = "HynTouch";

HynTouchVirtualGpioWriteCallback g_gpio_write_callback = nullptr;
HynTouchVirtualGpioReadCallback g_gpio_read_callback = nullptr;
void *g_gpio_callback_user_data = nullptr;
ExtensionIOXL9555 *g_xl9555 = nullptr;

bool write_virtual_xl9555_gpio(uint32_t gpio_id, bool value)
{
    if (!g_xl9555 || !XL9555_GPIO_IS((int)gpio_id)) {
        return false;
    }

    const uint8_t pin = XL9555_GPIO_TO_PIN(gpio_id);
    g_xl9555->pinMode(pin, OUTPUT);
    g_xl9555->digitalWrite(pin, value ? HIGH : LOW);
    return true;
}

bool read_virtual_xl9555_gpio(uint32_t gpio_id, int *out_value)
{
    if (!g_xl9555 || !out_value || !XL9555_GPIO_IS((int)gpio_id)) {
        return false;
    }

    const uint8_t pin = XL9555_GPIO_TO_PIN(gpio_id);
    *out_value = g_xl9555->digitalRead(pin) ? 1 : 0;
    return true;
}

} // namespace

int hyn_platform_gpio_set_value(uint32_t gpio_id, int value)
{
    if (g_gpio_write_callback && g_gpio_write_callback(gpio_id, value != 0, g_gpio_callback_user_data)) {
        return 1;
    }
    if (write_virtual_xl9555_gpio(gpio_id, value != 0)) {
        return 1;
    }
    if (XL9555_GPIO_IS((int)gpio_id)) {
        ESP_LOGE(kTag, "Virtual GPIO %lu needs a handler", (unsigned long)gpio_id);
        return -1;
    }
    return 0;
}

int hyn_platform_gpio_get_value(uint32_t gpio_id, int *out_value)
{
    if (!out_value) {
        return -1;
    }
    if (g_gpio_read_callback && g_gpio_read_callback(gpio_id, out_value, g_gpio_callback_user_data)) {
        return 1;
    }
    if (read_virtual_xl9555_gpio(gpio_id, out_value)) {
        return 1;
    }
    if (XL9555_GPIO_IS((int)gpio_id)) {
        ESP_LOGE(kTag, "Virtual GPIO %lu needs a handler", (unsigned long)gpio_id);
        return -1;
    }
    return 0;
}

void hyn_touch_set_virtual_gpio_callbacks(HynTouchVirtualGpioWriteCallback write_callback,
                                          HynTouchVirtualGpioReadCallback read_callback, void *user_data)
{
    g_gpio_write_callback = write_callback;
    g_gpio_read_callback = read_callback;
    g_gpio_callback_user_data = user_data;
}

void hyn_touch_attach_xl9555(ExtensionIOXL9555 *io)
{
    g_xl9555 = io;
}
