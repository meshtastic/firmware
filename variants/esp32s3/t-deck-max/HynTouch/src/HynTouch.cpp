#include "HynTouch.h"

#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>

#include <cstring>

#include <TDeckMaxBoard.h>

#include "hyn_core.h"

#ifndef HYN_TOUCH_RUNTIME_LOG
#define HYN_TOUCH_RUNTIME_LOG 0
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

namespace
{

constexpr const char *kTag = "HynTouch";

struct HynTouchRuntime {
    HynTouchConfig config = {};
    struct hyn_ts_data *data = nullptr;
    volatile bool press_flag = false;
    bool ready = false;
    bool isr_attached = false;
    int attached_irq = -1;
    bool key_pressed[3] = {false, false, false};
    bool key_seen[3] = {false, false, false};
    HynTouchKeyCallback key_callback = nullptr;
    void *key_callback_user_data = nullptr;
};

HynTouchRuntime g_touch;

void clear_key_state()
{
    memset(g_touch.key_pressed, 0, sizeof(g_touch.key_pressed));
    memset(g_touch.key_seen, 0, sizeof(g_touch.key_seen));
}

void apply_axis_transform(uint8_t point_count)
{
    if (!g_touch.data) {
        return;
    }

    for (u8 i = 0; i < point_count; ++i) {
        if (g_touch.data->plat_data.swap_xy) {
            u16 tmp = g_touch.data->rp_buf.pos_info[i].pos_x;
            g_touch.data->rp_buf.pos_info[i].pos_x = g_touch.data->rp_buf.pos_info[i].pos_y;
            g_touch.data->rp_buf.pos_info[i].pos_y = tmp;
        }
        if (g_touch.data->plat_data.reverse_x) {
            g_touch.data->rp_buf.pos_info[i].pos_x =
                g_touch.data->plat_data.x_resolution - g_touch.data->rp_buf.pos_info[i].pos_x;
        }
        if (g_touch.data->plat_data.reverse_y) {
            g_touch.data->rp_buf.pos_info[i].pos_y =
                g_touch.data->plat_data.y_resolution - g_touch.data->rp_buf.pos_info[i].pos_y;
        }
    }
}

void handle_key_report()
{
    if (!g_touch.data || (g_touch.data->rp_buf.report_need & REPORT_KEY) == 0) {
        return;
    }

    const int key_id = g_touch.data->rp_buf.key_id;
    if (key_id < 0 || key_id >= 3) {
        return;
    }

    const bool pressed = g_touch.data->rp_buf.key_state == 1;
    if (pressed) {
        g_touch.key_seen[key_id] = true;
    }

    if (g_touch.key_pressed[key_id] == pressed) {
        return;
    }

    g_touch.key_pressed[key_id] = pressed;
    if (g_touch.key_callback) {
        g_touch.key_callback((uint8_t)key_id, pressed, g_touch.key_callback_user_data);
    }
}

void IRAM_ATTR gpio_isr_handler(void *arg)
{
    (void)arg;
    g_touch.press_flag = true;
}

bool configure_irq_gpio(int irq_pin)
{
    if (irq_pin < 0) {
        ESP_LOGE(kTag, "Invalid touch IRQ pin: %d", irq_pin);
        return false;
    }

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_NEGEDGE;
    io_conf.pin_bit_mask = (1ULL << irq_pin);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    if (gpio_config(&io_conf) != ESP_OK) {
        ESP_LOGE(kTag, "Failed to configure touch IRQ pin %d", irq_pin);
        return false;
    }

    esp_err_t isr_ret = gpio_install_isr_service(0);
    if (isr_ret != ESP_OK && isr_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(kTag, "gpio_install_isr_service failed: %s", esp_err_to_name(isr_ret));
        return false;
    }

    if (g_touch.isr_attached && g_touch.attached_irq >= 0) {
        gpio_isr_handler_remove((gpio_num_t)g_touch.attached_irq);
    }

    esp_err_t handler_ret = gpio_isr_handler_add((gpio_num_t)irq_pin, gpio_isr_handler, nullptr);
    if (handler_ret != ESP_OK) {
        ESP_LOGE(kTag, "gpio_isr_handler_add failed: %s", esp_err_to_name(handler_ret));
        return false;
    }

    g_touch.isr_attached = true;
    g_touch.attached_irq = irq_pin;
    return true;
}

void detach_irq_handler()
{
    if (!g_touch.isr_attached || g_touch.attached_irq < 0) {
        return;
    }

    gpio_isr_handler_remove((gpio_num_t)g_touch.attached_irq);
    g_touch.isr_attached = false;
}

void attach_irq_handler()
{
    if (!g_touch.ready || g_touch.isr_attached || g_touch.attached_irq < 0) {
        return;
    }

    esp_err_t handler_ret = gpio_isr_handler_add((gpio_num_t)g_touch.attached_irq, gpio_isr_handler, nullptr);
    if (handler_ret != ESP_OK) {
        ESP_LOGE(kTag, "gpio_isr_handler_add failed after sleep: %s", esp_err_to_name(handler_ret));
        return;
    }

    g_touch.isr_attached = true;
}

bool initialize_touch_core(const HynTouchConfig &config)
{
    int ret = 0;
    static struct hyn_ts_data ts_data;
    memset(&ts_data, 0, sizeof(ts_data));

    g_touch.config = config;
    g_touch.data = &ts_data;
    g_touch.ready = false;
    g_touch.press_flag = false;
    clear_key_state();

    ESP_LOGI(kTag, HYN_DRIVER_VERSION);

    struct hyn_ts_fuc *support_touch_list[] = {
        (struct hyn_ts_fuc *)&cst66xx_fuc,
        (struct hyn_ts_fuc *)&cst3xx_fuc,
        (struct hyn_ts_fuc *)&cst226se_fuc,
    };

    g_touch.data->hyn_fuc_used = &cst66xx_fuc;
    g_touch.data->plat_data.max_touch_num = config.max_touch_points ? config.max_touch_points : MAX_POINTS_REPORT;
    g_touch.data->plat_data.irq_gpio = config.irq_pin;
    g_touch.data->plat_data.reset_gpio = config.reset_pin;
    g_touch.data->plat_data.x_resolution = config.x_resolution;
    g_touch.data->plat_data.y_resolution = config.y_resolution;
    g_touch.data->plat_data.swap_xy = config.swap_xy ? 1 : 0;
    g_touch.data->plat_data.reverse_x = config.reverse_x ? 1 : 0;
    g_touch.data->plat_data.reverse_y = config.reverse_y ? 1 : 0;

    if (g_touch.data->plat_data.reset_gpio >= 0 && !XL9555_GPIO_IS(g_touch.data->plat_data.reset_gpio)) {
        gpio_config_t io_conf = {};
        io_conf.intr_type = GPIO_INTR_DISABLE;
        io_conf.mode = GPIO_MODE_OUTPUT;
        io_conf.pin_bit_mask = (1ULL << g_touch.data->plat_data.reset_gpio);
        io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
        io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        if (gpio_config(&io_conf) != ESP_OK) {
            ESP_LOGE(kTag, "Failed to configure touch reset pin %d", g_touch.data->plat_data.reset_gpio);
            return false;
        }
    }

    esp_err_t i2c_ret = hyn_i2c_init((u8)config.sda_pin, (u8)config.scl_pin);
    if (i2c_ret != ESP_OK) {
        ESP_LOGE(kTag, "I2C init failed: %s", esp_err_to_name(i2c_ret));
        return false;
    }

    for (size_t i = 0; i < ARRAY_SIZE(support_touch_list); ++i) {
        g_touch.data->hyn_fuc_used = support_touch_list[i];
        ret = g_touch.data->hyn_fuc_used->tp_chip_init(g_touch.data);
        if (ret == 0) {
            ESP_LOGI(kTag, "Touch init SUCCEED");
            ESP_LOGI(kTag, "IC_info fw_project_id:%lx", g_touch.data->hw_info.fw_project_id);
            ESP_LOGI(kTag, "ictype:[%lx]", g_touch.data->hw_info.fw_chip_type);
            ESP_LOGI(kTag, "fw_ver:%lx", g_touch.data->hw_info.fw_ver);
            break;
        }
    }

    if (ret != 0) {
        ESP_LOGE(kTag, "Touch probe failed");
        return false;
    }

    if (!configure_irq_gpio(config.irq_pin)) {
        return false;
    }

    g_touch.ready = true;
    return true;
}

} // namespace

HynTouchConfig hyn_touch_default_config()
{
    HynTouchConfig config = {};
    config.sda_pin = BOARD_TOUCH_SDA;
    config.scl_pin = BOARD_TOUCH_SCL;
    config.reset_pin = BOARD_TOUCH_RST;
    config.irq_pin = BOARD_TOUCH_INT;
    config.max_touch_points = MAX_POINTS_REPORT;
    config.x_resolution = LCD_HOR_SIZE;
    config.y_resolution = LCD_VER_SIZE;
    config.swap_xy = false;
    config.reverse_x = false;
    config.reverse_y = false;
    return config;
}

bool hyn_touch_init()
{
    const HynTouchConfig config = hyn_touch_default_config();
    return hyn_touch_init_with_config(&config);
}

bool hyn_touch_init_with_config(const HynTouchConfig *config)
{
    if (config == nullptr) {
        return false;
    }

    return initialize_touch_core(*config);
}

bool hyn_touch_is_ready()
{
    return g_touch.ready;
}

uint8_t hyn_touch_get_point(int16_t *x_array, int16_t *y_array, uint8_t get_point)
{
    if (!g_touch.ready || !g_touch.data || x_array == nullptr || y_array == nullptr || get_point == 0) {
        return 0;
    }

    if (!g_touch.press_flag) {
        return 0;
    }
    g_touch.press_flag = false;

    uint8_t point_count = 0;
    g_touch.data->hyn_irq_flg = 1;
    if (g_touch.data->work_mode < DIFF_MODE) {
        const int ret = g_touch.data->hyn_fuc_used->tp_report();
        point_count = (g_touch.data->rp_buf.report_need & REPORT_POS) ? g_touch.data->rp_buf.rep_num : 0;
        apply_axis_transform(point_count);

        for (uint8_t i = 0; i < point_count && i < get_point; ++i) {
            x_array[i] = g_touch.data->rp_buf.pos_info[i].pos_x;
            y_array[i] = g_touch.data->rp_buf.pos_info[i].pos_y;
        }

#if HYN_TOUCH_RUNTIME_LOG
        printf("ret:%d num:%d xy:", ret, point_count);
        for (uint8_t i = 0; i < point_count; ++i) {
            printf("(%d,%d) ", g_touch.data->rp_buf.pos_info[i].pos_x, g_touch.data->rp_buf.pos_info[i].pos_y);
        }
        printf("key_id:%d, key_st:%d\n", g_touch.data->rp_buf.key_id, g_touch.data->rp_buf.key_state);
#else
        (void)ret;
#endif
    }

    handle_key_report();
    g_touch.data->rp_buf.report_need = REPORT_NONE;
    return point_count;
}

bool hyn_touch_get_key_state(uint8_t key_id)
{
    if (key_id >= 3) {
        return false;
    }
    return g_touch.key_pressed[key_id];
}

bool hyn_touch_get_key_seen(uint8_t key_id)
{
    if (key_id >= 3) {
        return false;
    }
    return g_touch.key_seen[key_id];
}

void hyn_touch_clear_key_seen()
{
    memset(g_touch.key_seen, 0, sizeof(g_touch.key_seen));
}

void hyn_touch_set_key_callback(HynTouchKeyCallback callback, void *user_data)
{
    g_touch.key_callback = callback;
    g_touch.key_callback_user_data = user_data;
}

void hyn_touch_before_light_sleep()
{
    detach_irq_handler();
    g_touch.press_flag = false;
}

void hyn_touch_after_light_sleep()
{
    g_touch.press_flag = false;
    hyn_touch_resume();
    g_touch.press_flag = false;
    attach_irq_handler();
}

void hyn_touch_resume()
{
    if (!g_touch.ready || !g_touch.data || !g_touch.data->hyn_fuc_used || !g_touch.data->hyn_fuc_used->tp_resum) {
        return;
    }

    g_touch.data->hyn_fuc_used->tp_resum();
}

void hyn_sleep()
{
    if (!g_touch.ready || !g_touch.data || !g_touch.data->hyn_fuc_used || !g_touch.data->hyn_fuc_used->tp_supend) {
        return;
    }

#if HYN_TOUCH_RUNTIME_LOG
    printf("hyn_sleep = %p\n", g_touch.data->hyn_fuc_used->tp_supend);
#endif
    g_touch.data->hyn_fuc_used->tp_supend();
    delay(100);
}
