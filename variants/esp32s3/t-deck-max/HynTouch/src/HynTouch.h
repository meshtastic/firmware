#pragma once

#include <Arduino.h>
#include <stdint.h>

class ExtensionIOXL9555;

typedef void (*HynTouchKeyCallback)(uint8_t key_id, bool pressed, void *user_data);
typedef bool (*HynTouchVirtualGpioWriteCallback)(uint32_t gpio_id, bool value, void *user_data);
typedef bool (*HynTouchVirtualGpioReadCallback)(uint32_t gpio_id, int *value, void *user_data);

struct HynTouchConfig {
    int sda_pin;
    int scl_pin;
    int reset_pin;
    int irq_pin;
    uint8_t max_touch_points;
    uint16_t x_resolution;
    uint16_t y_resolution;
    bool swap_xy;
    bool reverse_x;
    bool reverse_y;
};

HynTouchConfig hyn_touch_default_config();
bool hyn_touch_init();
bool hyn_touch_init_with_config(const HynTouchConfig *config);
bool hyn_touch_is_ready();
uint8_t hyn_touch_get_point(int16_t *x_array, int16_t *y_array, uint8_t get_point);
bool hyn_touch_get_key_state(uint8_t key_id);
bool hyn_touch_get_key_seen(uint8_t key_id);
void hyn_touch_clear_key_seen();
void hyn_touch_set_key_callback(HynTouchKeyCallback callback, void *user_data);
void hyn_touch_set_virtual_gpio_callbacks(HynTouchVirtualGpioWriteCallback write_callback,
                                          HynTouchVirtualGpioReadCallback read_callback, void *user_data);
void hyn_touch_attach_xl9555(ExtensionIOXL9555 *io);
void hyn_touch_before_light_sleep();
void hyn_touch_after_light_sleep();
void hyn_touch_resume();
void hyn_sleep();
