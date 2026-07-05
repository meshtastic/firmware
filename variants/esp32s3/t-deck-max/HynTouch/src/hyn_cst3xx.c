#include "hyn_core.h"

#define BOOT_I2C_ADDR (0x1A)
#define MAIN_I2C_ADDR (0x1A)
#define RW_REG_LEN (2)
#define CST3xx_BIN_SIZE (24 * 1024 + 24)
#define SOFT_RST_ENABLE (0)

static const char *TAG = "hyn_cst3xx";
static struct hyn_ts_data *hyn_3xxdata = NULL;
static const u8 gest_map_tbl[33] = {0xff, 4,  1, 3, 2,    5,    12,   6,    7,    7,    9,    11,   10,   13,   12,   7, 7,
                                    6,    10, 6, 5, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 14};

static int cst3xx_updata_tpinfo(void);
static int cst3xx_set_workmode(enum work_mode mode, u8 enable);
static void cst3xx_rst(void);

static int cst3xx_init(struct hyn_ts_data *ts_data)
{
    int ret = 0;
    hyn_3xxdata = ts_data;
    hyn_set_i2c_addr(hyn_3xxdata, MAIN_I2C_ADDR);
    cst3xx_rst();
    mdelay(50);
    ret = cst3xx_updata_tpinfo();
    ret |= cst3xx_set_workmode(NOMAL_MODE, 0);
    return ret;
}

static int cst3xx_report(void)
{
    u8 buf[80];
    u8 finger_num = 0, len = 0, write_tail_end = 0, key_state = 0, key_id = 0, tmp_dat;
    struct hyn_plat_data *dt = &hyn_3xxdata->plat_data;
    int ret = 0;
    hyn_3xxdata->rp_buf.report_need = REPORT_NONE;
    switch (hyn_3xxdata->work_mode) {
    case NOMAL_MODE:
        write_tail_end = 1;
        ret = hyn_wr_reg(hyn_3xxdata, 0xD000, 2, buf, 7);
        if (ret || buf[6] != 0xAB || buf[0] == 0xAB) {
            break;
        }
        len = buf[5] & 0x7F;
        if (len > dt->max_touch_num) {
            len = dt->max_touch_num;
        }
        if (buf[5] & 0x80) { // key
            if (buf[5] == 0x80) {
                key_id = (buf[1] >> 4) - 1;
                key_state = buf[0];
            } else {
                finger_num = len;
                len = (len - 1) * 5 + 3;
                ret = hyn_wr_reg(hyn_3xxdata, 0xD007, 2, &buf[5], len);
                len += 5;
                key_id = (buf[len - 2] >> 4) - 1;
                key_state = buf[len - 3];
            }
            if (key_state & 0x80) {
                hyn_3xxdata->rp_buf.report_need |= REPORT_KEY;
                if ((key_id == hyn_3xxdata->rp_buf.key_id || 0 == hyn_3xxdata->rp_buf.key_state) && key_state == 0x83) {
                    hyn_3xxdata->rp_buf.key_id = key_id;
                    hyn_3xxdata->rp_buf.key_state = 1;
                } else {
                    hyn_3xxdata->rp_buf.key_state = 0;
                }
            }
        } else { // pos
            u16 index = 0;
            u8 i = 0;
            finger_num = len;
            hyn_3xxdata->rp_buf.report_need |= REPORT_POS;
            if (finger_num > 1) {
                len = (len - 1) * 5 + 1;
                ret = hyn_wr_reg(hyn_3xxdata, 0xD007, 2, &buf[5], len);
            }
            hyn_3xxdata->rp_buf.rep_num = finger_num;
            for (i = 0; i < finger_num; i++) {
                index = i * 5;
                hyn_3xxdata->rp_buf.pos_info[i].pos_id = (buf[index] >> 4) & 0x0F;
                hyn_3xxdata->rp_buf.pos_info[i].event = (buf[index] & 0x0F) == 0x06 ? 1 : 0;
                hyn_3xxdata->rp_buf.pos_info[i].pos_x = ((u16)buf[index + 1] << 4) + ((buf[index + 3] >> 4) & 0x0F);
                hyn_3xxdata->rp_buf.pos_info[i].pos_y = ((u16)buf[index + 2] << 4) + (buf[index + 3] & 0x0F);
                hyn_3xxdata->rp_buf.pos_info[i].pres_z = buf[index + 4];
                // HYN_INFO("report_id = %d, xy =
                // %d,%d",hyn_3xxdata->rp_buf.pos_info[i].pos_id,hyn_3xxdata->rp_buf.pos_info[i].pos_x,hyn_3xxdata->rp_buf.pos_info[i].pos_y);
            }
        }
        break;
    case GESTURE_MODE:
        ret = hyn_wr_reg(hyn_3xxdata, 0xD04C, 2, &tmp_dat, 1);
        if ((tmp_dat & 0x7F) <= 32) {
            tmp_dat = tmp_dat & 0x7F;
            hyn_3xxdata->gesture_id = gest_map_tbl[tmp_dat];
            hyn_3xxdata->rp_buf.report_need |= REPORT_GES;
        }
        break;
    default:
        break;
    }
    if (write_tail_end) {
        hyn_wr_reg(hyn_3xxdata, 0xD000AB, 3, buf, 0);
    }
    return 0;
}

static int cst3xx_set_workmode(enum work_mode mode, u8 enable)
{
    int ret = 0;
    hyn_3xxdata->work_mode = mode;
    if (mode != NOMAL_MODE)
        hyn_esdcheck_switch(hyn_3xxdata, DISABLE);
    switch (mode) {
    case NOMAL_MODE:
        hyn_irq_set(hyn_3xxdata, ENABLE);
        hyn_esdcheck_switch(hyn_3xxdata, enable);
        // hyn_wr_reg(hyn_3xxdata,0xD100,2,NULL,0);
        hyn_wr_reg(hyn_3xxdata, 0xD109, 2, NULL, 0);
        break;
    case GESTURE_MODE:
        hyn_wr_reg(hyn_3xxdata, 0xD04C80, 3, NULL, 0);
        break;
    case LP_MODE:
        break;
    case DIFF_MODE:
        hyn_wr_reg(hyn_3xxdata, 0xD10D, 2, NULL, 0);
        break;
    case RAWDATA_MODE:
        hyn_wr_reg(hyn_3xxdata, 0xD10A, 2, NULL, 0);
        break;
    case FAC_TEST_MODE:
        hyn_wr_reg(hyn_3xxdata, 0xD119, 2, NULL, 0);
        break;
    case DEEPSLEEP:
        hyn_irq_set(hyn_3xxdata, DISABLE);
        hyn_wr_reg(hyn_3xxdata, 0xD105, 2, NULL, 0);
        break;
    default:
        hyn_esdcheck_switch(hyn_3xxdata, ENABLE);
        hyn_3xxdata->work_mode = NOMAL_MODE;
        break;
    }
    return ret;
}

static int cst3xx_supend(void)
{
    HYN_ENTER();
    // cst3xx_set_workmode(DEEPSLEEP,0);
    ESP_LOGI(TAG, "touch sleep");

    hyn_irq_set(hyn_3xxdata, DISABLE);
    hyn_wr_reg(hyn_3xxdata, 0xD105, 2, NULL, 0);
    return 0;
}

static int cst3xx_resum(void)
{
    HYN_ENTER();
    cst3xx_rst();
    msleep(50);
    cst3xx_set_workmode(NOMAL_MODE, 0);
    return 0;
}

static void cst3xx_rst(void)
{
    if (hyn_3xxdata->work_mode == ENTER_BOOT_MODE) {
        hyn_set_i2c_addr(hyn_3xxdata, MAIN_I2C_ADDR);
    }
#if SOFT_RST_ENABLE
    hyn_wr_reg(hyn_3xxdata, 0xD10E, 2, NULL, 0);
#endif
    gpio_set_value(hyn_3xxdata->plat_data.reset_gpio, 0);
    msleep(10);
    gpio_set_value(hyn_3xxdata->plat_data.reset_gpio, 1);
}

static int cst3xx_updata_tpinfo(void)
{
    u8 buf[28];
    struct tp_info *ic = &hyn_3xxdata->hw_info;
    int ret = 0;
    ret = hyn_wr_reg(hyn_3xxdata, 0xD101, 2, buf, 0);
    mdelay(1);
    ret |= hyn_wr_reg(hyn_3xxdata, 0xD1F4, 2, buf, 28);
    if (ret) {
        ESP_LOGE(TAG, "cst3xx_updata_tpinfo failed");
        return -1;
    }

    ic->fw_sensor_txnum = buf[0];
    ic->fw_sensor_rxnum = buf[2];
    ic->fw_key_num = buf[3];
    ic->fw_res_y = (buf[7] << 8) | buf[6];
    ic->fw_res_x = (buf[5] << 8) | buf[4];
    ic->fw_project_id = (buf[17] << 8) | buf[16];
    ic->fw_chip_type = (buf[19] << 8) | buf[18];
    ic->fw_ver = (buf[23] << 24) | (buf[22] << 16) | (buf[21] << 8) | buf[20];

    ESP_LOGI(TAG, "IC_info fw_project_id:%04lx ictype:%04lx fw_ver:%lx checksum:%#lx", ic->fw_project_id, ic->fw_chip_type,
             ic->fw_ver, ic->ic_fw_checksum);
    return 0;
}

const struct hyn_ts_fuc cst3xx_fuc = {
    .tp_rest = cst3xx_rst,
    .tp_report = cst3xx_report,
    .tp_supend = cst3xx_supend,
    .tp_resum = cst3xx_resum,
    .tp_chip_init = cst3xx_init,
    .tp_updata_fw = NULL,
    .tp_set_workmode = cst3xx_set_workmode,
    .tp_check_esd = NULL,
    .tp_prox_handle = NULL,
    .tp_get_dbg_data = NULL,
    .tp_get_test_result = NULL,
};
