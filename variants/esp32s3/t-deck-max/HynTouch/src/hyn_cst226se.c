#include "hyn_core.h"

#define BOOT_I2C_ADDR (0x5A)
// #define MAIN_I2C_ADDR   (0x5A)
#define MAIN_I2C_ADDR (0x1A)

#define RW_REG_LEN (2)

#define CST226SE_BIN_SIZE (7 * 1024 + 512)

static const char *TAG = "hyn_cst226se";
static struct hyn_ts_data *hyn_226data = NULL;
static const u8 gest_map_tbl[33] = {0xff, 4,  1, 3, 2,    5,    12,   6,    7,    7,    9,    11,   10,   13,   12,   7, 7,
                                    6,    10, 6, 5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 14};

static int cst226se_updata_tpinfo(void);
static int cst226se_set_workmode(enum work_mode mode, u8 enable);
static void cst226se_rst(void);

static int cst226se_init(struct hyn_ts_data *ts_data)
{
    int ret = 0;
    hyn_226data = ts_data;
    cst226se_rst();
    mdelay(50);
    hyn_set_i2c_addr(hyn_226data, MAIN_I2C_ADDR);
    ret = cst226se_updata_tpinfo();
    ret |= cst226se_set_workmode(NOMAL_MODE, 0);
    return ret;
}

static int cst226se_report(void)
{
    u8 buf[80] = {0};
    u8 finger_num = 0, key_flg = 0, tmp_dat;
    int len = 0;
    struct hyn_plat_data *dt = &hyn_226data->plat_data;
    int ret = 0, retry = 2;
    switch (hyn_226data->work_mode) {
    case NOMAL_MODE:
        retry = 2;
        while (retry--) {
            ret = hyn_wr_reg(hyn_226data, 0xD000, 2, buf, 7);
            if (ret || buf[6] != 0xAB || buf[0] == 0xAB) {
                ret = -2;
                continue;
            }
            finger_num = buf[5] & 0x7F;
            if (finger_num > dt->max_touch_num) {
                finger_num = dt->max_touch_num;
            }
            key_flg = (buf[5] & 0x80) ? 1 : 0;
            len = 0;
            if (finger_num > 1) {
                len += (finger_num - 1) * 5;
            }
            if (key_flg && finger_num) {
                len += 3;
            }
            if (len > 0) {
                ret = hyn_wr_reg(hyn_226data, 0xD007, 2, &buf[5], len);
            }
            ret |= hyn_wr_reg(hyn_226data, 0xD000AB, 3, buf, 0);
            if (ret) {
                ret = -3;
                continue;
            }
            ret = 0;
            break;
        }
        if (ret) {
            hyn_wr_reg(hyn_226data, 0xD000AB, 3, buf, 0);
            ESP_LOGE(TAG, "read frame failed");
            break;
        }
        if (key_flg) { // key
            if (hyn_226data->rp_buf.report_need == REPORT_NONE) {
                hyn_226data->rp_buf.report_need |= REPORT_KEY;
            }
            len = finger_num ? (len + 5) : 3;
            hyn_226data->rp_buf.key_id = (buf[len - 2] >> 4) - 1;
            hyn_226data->rp_buf.key_state = (buf[len - 3] & 0x0F) == 0x03 ? 1 : 0;
            ESP_LOGI(TAG, "key_id:%x state:%x", hyn_226data->rp_buf.key_id, hyn_226data->rp_buf.key_state);
        }
        if (finger_num) {
            u16 index = 0, i = 0;
            u8 touch_down = 0;
            if (hyn_226data->rp_buf.report_need == REPORT_NONE) {
                hyn_226data->rp_buf.report_need |= REPORT_POS;
            }
            hyn_226data->rp_buf.rep_num = finger_num;
            for (i = 0; i < finger_num; i++) {
                index = i * 5;
                hyn_226data->rp_buf.pos_info[i].pos_id = (buf[index] >> 4) & 0x0F;
                hyn_226data->rp_buf.pos_info[i].event = (buf[index] & 0x0F) == 0x06 ? 1 : 0;
                hyn_226data->rp_buf.pos_info[i].pos_x = ((u16)buf[index + 1] << 4) + ((buf[index + 3] >> 4) & 0x0F);
                hyn_226data->rp_buf.pos_info[i].pos_y = ((u16)buf[index + 2] << 4) + (buf[index + 3] & 0x0F);
                hyn_226data->rp_buf.pos_info[i].pres_z = buf[index + 4];
                if (hyn_226data->rp_buf.pos_info[i].event)
                    touch_down++;
                // ESP_LOGI(TAG, "report_id = %d, xy =
                // %d,%d",hyn_226data->rp_buf.pos_info[i].pos_id,hyn_226data->rp_buf.pos_info[i].pos_x,hyn_226data->rp_buf.pos_info[i].pos_y);
            }
            if (0 == touch_down) {
                hyn_226data->rp_buf.rep_num = 0;
            }
        }
        break;
    case GESTURE_MODE:
        ret = hyn_wr_reg(hyn_226data, 0xD04C, 2, &tmp_dat, 1);
        if ((tmp_dat & 0x7F) <= 32) {
            tmp_dat = tmp_dat & 0x7F;
            hyn_226data->gesture_id = gest_map_tbl[tmp_dat];
            hyn_226data->rp_buf.report_need |= REPORT_GES;
        }
        break;
    default:
        break;
    }
    return 0;
}

static int cst226se_set_workmode(enum work_mode mode, u8 enable)
{
    int ret = 0;
    ESP_LOGI(TAG, "enter %s", __func__);
    hyn_226data->work_mode = mode;
    if (mode != NOMAL_MODE)
        hyn_esdcheck_switch(hyn_226data, DISABLE);
    switch (mode) {
    case NOMAL_MODE:
        hyn_irq_set(hyn_226data, ENABLE);
        hyn_esdcheck_switch(hyn_226data, enable);
        hyn_wr_reg(hyn_226data, 0xD10B, 2, NULL, 0); // soft rst
        hyn_wr_reg(hyn_226data, 0xD109, 2, NULL, 0);
        break;
    case GESTURE_MODE:
        hyn_wr_reg(hyn_226data, 0xD04C80, 3, NULL, 0);
        break;
    case LP_MODE:
        break;
    case DIFF_MODE:
        hyn_wr_reg(hyn_226data, 0xD10B, 2, NULL, 0);
        hyn_wr_reg(hyn_226data, 0xD10D, 2, NULL, 0);
        break;
    case RAWDATA_MODE:
        hyn_wr_reg(hyn_226data, 0xD10B, 2, NULL, 0);
        hyn_wr_reg(hyn_226data, 0xD10A, 2, NULL, 0);
        break;
    case FAC_TEST_MODE:
        hyn_wr_reg(hyn_226data, 0xD10B, 2, NULL, 0);
        hyn_wr_reg(hyn_226data, 0xD119, 2, NULL, 0);
        msleep(50); // wait  switch to fac mode
        break;
    case DEEPSLEEP:
        hyn_irq_set(hyn_226data, DISABLE);
        hyn_wr_reg(hyn_226data, 0xD105, 2, NULL, 0);
        break;
    default:
        hyn_esdcheck_switch(hyn_226data, ENABLE);
        hyn_226data->work_mode = NOMAL_MODE;
        break;
    }
    return ret;
}

static int cst226se_supend(void)
{
    ESP_LOGI(TAG, "touch sleep");

    // cst226se_set_workmode(DEEPSLEEP,0);

    hyn_irq_set(hyn_226data, DISABLE);
    hyn_wr_reg(hyn_226data, 0xD105, 2, NULL, 0);
    return 0;
}

static int cst226se_resum(void)
{
    ESP_LOGI(TAG, "enter %s", __func__);
    cst226se_rst();
    msleep(50);
    cst226se_set_workmode(NOMAL_MODE, 0);
    return 0;
}

static void cst226se_rst(void)
{
    if (hyn_226data->work_mode == ENTER_BOOT_MODE) {
        hyn_set_i2c_addr(hyn_226data, MAIN_I2C_ADDR);
    }
    gpio_set_value(hyn_226data->plat_data.reset_gpio, 0);
    msleep(10);
    gpio_set_value(hyn_226data->plat_data.reset_gpio, 1);
}

static int cst226se_updata_tpinfo(void)
{
    u8 buf[28];
    struct tp_info *ic = &hyn_226data->hw_info;
    int ret = 0, retry = 5;
    while (--retry) {
        ret = hyn_wr_reg(hyn_226data, 0xD101, 2, buf, 0);
        mdelay(1);
        ret |= hyn_wr_reg(hyn_226data, 0xD1F4, 2, buf, 28);
        cst226se_set_workmode(NOMAL_MODE, 0);
        if (ret == 0 && U8TO16(buf[19], buf[18]) == 0x00a8) {
            break;
        }
        msleep(1);
    }

    if (ret || retry == 0) {
        ESP_LOGE(TAG, "cst226se_updata_tpinfo failed");

        for (int i = 0; i < 28; i++) {
            printf("%x ", buf[i]);
        }
        return -1;
    }

    ic->fw_sensor_txnum = buf[0];
    ic->fw_sensor_rxnum = buf[2];
    ic->fw_key_num = buf[3];
    ic->fw_res_y = (buf[7] << 8) | buf[6];
    ic->fw_res_x = (buf[5] << 8) | buf[4];
    ic->fw_project_id = (buf[17] << 8) | buf[16];
    ic->fw_chip_type = U8TO16(buf[19], buf[18]);
    ic->fw_ver = (buf[23] << 24) | (buf[22] << 16) | (buf[21] << 8) | buf[20];

    ESP_LOGI(TAG, "IC_info fw_project_id:%04lx ictype:%04lx fw_ver:%lx checksum:%#lx", ic->fw_project_id, ic->fw_chip_type,
             ic->fw_ver, ic->ic_fw_checksum);
    return 0;
}

const struct hyn_ts_fuc cst226se_fuc = {
    .tp_rest = cst226se_rst,
    .tp_report = cst226se_report,
    .tp_supend = cst226se_supend,
    .tp_resum = cst226se_resum,
    .tp_chip_init = cst226se_init,
    .tp_updata_fw = NULL,
    .tp_set_workmode = cst226se_set_workmode,
    .tp_check_esd = NULL,
    .tp_prox_handle = NULL,
    .tp_get_dbg_data = NULL,
    .tp_get_test_result = NULL,
};
