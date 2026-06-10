#include "esp32-hal-i2c.h"
#include "hyn_core.h"
#include "hyn_platform.h"

#define DATA_LENGTH 100

#define I2C_MASTER_NUM                                                                                                           \
    0 /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ 200000 /*!< I2C master clock frequency */
#define I2C_MASTER_TIMEOUT_MS 1000

esp_err_t hyn_i2c_init(u8 pin_sda, u8 pin_scl)
{
    if (i2cIsInit(I2C_MASTER_NUM)) {
        return ESP_OK;
    }

    return i2cInit(I2C_MASTER_NUM, pin_sda, pin_scl, I2C_MASTER_FREQ_HZ);
}

int hyn_write_data(struct hyn_ts_data *ts_data, u8 *buf, u8 reg_len, u16 len)
{
    (void)reg_len;
    esp_err_t ret = i2cWrite(I2C_MASTER_NUM, ts_data->salve_addr, buf, len, I2C_MASTER_TIMEOUT_MS);
    return ret == ESP_OK ? 0 : -1;
}

int hyn_read_data(struct hyn_ts_data *ts_data, u8 *buf, u16 len)
{
    size_t readCount = 0;
    esp_err_t ret = i2cRead(I2C_MASTER_NUM, ts_data->salve_addr, buf, len, I2C_MASTER_TIMEOUT_MS, &readCount);
    return ret == ESP_OK && readCount == len ? 0 : -1;
}

int hyn_wr_reg(struct hyn_ts_data *ts_data, u32 reg_addr, u8 reg_len, u8 *rbuf, u16 rlen)
{
    int ret = 0, i = 0;
    u8 wbuf[4];
    reg_len = reg_len & 0x0F;
    memset(wbuf, 0, sizeof(wbuf));
    i = reg_len;
    while (i) {
        i--;
        wbuf[i] = reg_addr;
        reg_addr >>= 8;
    }
    size_t readCount = 0;
    if (rlen) {
        ret = i2cWriteReadNonStop(I2C_MASTER_NUM, ts_data->salve_addr, wbuf, reg_len, rbuf, rlen, I2C_MASTER_TIMEOUT_MS,
                                  &readCount);
        return ret == ESP_OK && readCount == rlen ? 0 : -1;
    }

    ret = i2cWrite(I2C_MASTER_NUM, ts_data->salve_addr, wbuf, reg_len, I2C_MASTER_TIMEOUT_MS);
    return ret == ESP_OK ? 0 : -1;
}

void hyn_delay_ms(int cnt)
{
    vTaskDelay(cnt / portTICK_PERIOD_MS);
}

/**gpio ctl*/
int gpio_set_value(uint32_t gpio_id, bool vlue)
{
    int handled = hyn_platform_gpio_set_value(gpio_id, vlue ? 1 : 0);
    if (handled > 0) {
        return 0;
    }
    if (handled < 0 || (int32_t)gpio_id < 0) {
        return -1;
    }
    gpio_set_level(gpio_id, vlue);
    return 0;
}
bool gpio_get_value(uint32_t gpio_id)
{
    int value = 0;
    int handled = hyn_platform_gpio_get_value(gpio_id, &value);
    if (handled > 0) {
        return value ? true : false;
    }
    if (handled < 0 || (int32_t)gpio_id < 0) {
        return false;
    }
    return gpio_get_level(gpio_id);
}
