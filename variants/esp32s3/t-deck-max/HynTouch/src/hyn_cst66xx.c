#include "hyn_core.h"

#define BOOT_I2C_ADDR (0x5A)
// #define MAIN_I2C_ADDR   (0x58) //use 2 slave addr
#define MAIN_I2C_ADDR (0x1A) // use 2 slave addr

#define PART_NO_EN (0)
#define cst66xx_BIN_SIZE (40 * 1024) //(36*1024)
#define MODULE_ID_ADDR (0xA400)
#define PARTNUM_ADDR (0xFF10)

static const char *TAG = "hyn_cst66xx";
static struct hyn_ts_data *hyn_66xxdata = NULL;
static const u8 gest_map_tbl[] = {
    IDX_POWER, // GESTURE_LABEL_CLICK,
    IDX_POWER, // GESTURE_LABEL_CLICK2,
    IDX_UP,    // GESTURE_LABEL_TOP,
    IDX_DOWN,  // GESTURE_LABEL_BOTTOM,
    IDX_LEFT,  // GESTURE_LABEL_LEFT,
    IDX_RIGHT, // GESTURE_LABEL_RIGHT,
    IDX_C,     // GESTURE_LABEL_C,
    IDX_e,     // GESTURE_LABEL_E,
    IDX_V,     // GESTURE_LABEL_v,
    IDX_NULL,  // GESTURE_LABEL_^,
    IDX_NULL,  // GESTURE_LABEL_>,
    IDX_NULL,  // GESTURE_LABEL_<,
    IDX_M,     // GESTURE_LABEL_M,
    IDX_W,     // GESTURE_LABEL_W,
    IDX_O,     // GESTURE_LABEL_O,
    IDX_S,     // GESTURE_LABEL_S,
    IDX_Z,     // GESTURE_LABEL_Z
};

static void cst66xx_rst(void);
static int cst66xx_set_workmode(enum work_mode mode, u8 enable);
static int cst66xx_updata_tpinfo(void);

static int cst66xx_init(struct hyn_ts_data *ts_data)
{
    int ret = 0;
    HYN_ENTER();
    hyn_66xxdata = ts_data;
    hyn_set_i2c_addr(hyn_66xxdata, MAIN_I2C_ADDR);
    cst66xx_rst(); // exit boot
    mdelay(50);
    ret = cst66xx_updata_tpinfo();
    if (ret)
        ESP_LOGE(TAG, "get tpinfo failed");
    ret |= cst66xx_set_workmode(NOMAL_MODE, 0);
    return ret;
}

static int cst66xx_report(void)
{
    u8 buf[80], i = 0;
    u8 finger_num = 0, key_num = 0, report_typ = 0, key_state = 0, key_id = 0, tmp_dat = 0;
    int ret = 0, retry = 2;

    while (retry--) { // read point
        ret = hyn_wr_reg(hyn_66xxdata, 0xD0070000, 0x80 | 4, buf, 9);
        report_typ = buf[2]; // FF:pos F0:ges E0:prox
        finger_num = buf[3] & 0x0f;
        key_num = ((buf[3] & 0xf0) >> 4);
        if (finger_num + key_num <= MAX_POINTS_REPORT) {
            if (key_num + finger_num > 1) {
                ret |= hyn_read_data(hyn_66xxdata, &buf[9], (key_num + finger_num - 1) * 5);
            }
            if (hyn_sum16(0x55, &buf[4], (key_num + finger_num) * 5) != (buf[0] | buf[1] << 8)) {
                ret = -1;
            }
        } else {
            ret = -2;
        }
        if (ret && retry)
            continue;
        ret |= hyn_wr_reg(hyn_66xxdata, 0xD00002AB, 4, buf, 0);
        if (ret == 0) {
            break;
        }
    }
    if (ret)
        return ret;

    if ((report_typ == 0xff) && ((finger_num + key_num) > 0)) {
        if (key_num) {
            key_id = buf[8] & 0x0f;
            key_state = buf[8] >> 4;
            if (hyn_66xxdata->rp_buf.report_need == REPORT_NONE) { // Mutually exclusive reporting of coordinates and keys
                hyn_66xxdata->rp_buf.report_need |= REPORT_KEY;
            }
            hyn_66xxdata->rp_buf.key_id = key_id;
            hyn_66xxdata->rp_buf.key_state = key_state != 0 ? 1 : 0;
        }

        if (finger_num) { // pos
            u16 index = 0;
            u8 touch_down = 0;
            if (hyn_66xxdata->rp_buf.report_need == REPORT_NONE) { // Mutually exclusive reporting of coordinates and keys
                hyn_66xxdata->rp_buf.report_need |= REPORT_POS;
            }
            hyn_66xxdata->rp_buf.rep_num = finger_num;
            for (i = 0; i < finger_num; i++) {
                index = (key_num + i) * 5;
                hyn_66xxdata->rp_buf.pos_info[i].pos_id = buf[index + 8] & 0x0F;
                hyn_66xxdata->rp_buf.pos_info[i].event = buf[index + 8] >> 4;
                hyn_66xxdata->rp_buf.pos_info[i].pos_x = buf[index + 4] + ((u16)(buf[index + 7] & 0x0F) << 8); // x is rx
                                                                                                               // direction
                hyn_66xxdata->rp_buf.pos_info[i].pos_y = buf[index + 5] + ((u16)(buf[index + 7] & 0xf0) << 4);
                hyn_66xxdata->rp_buf.pos_info[i].pres_z = buf[index + 6];
                if (hyn_66xxdata->rp_buf.pos_info[i].event) {
                    touch_down++;
                }
            }
            if (0 == touch_down) {
                hyn_66xxdata->rp_buf.rep_num = 0;
            }
        }
    } else if (report_typ == 0xF0) {
        tmp_dat = buf[8] & 0xff;
        if ((tmp_dat & 0x7F) < sizeof(gest_map_tbl) && gest_map_tbl[tmp_dat] != IDX_NULL) {
            hyn_66xxdata->gesture_id = gest_map_tbl[tmp_dat];
            hyn_66xxdata->rp_buf.report_need |= REPORT_GES;
            ESP_LOGI(TAG, "gesture_id:%d", tmp_dat);
        }
    } else if (report_typ == 0xE0) { // proximity
        u8 state = buf[4] ? PS_FAR_AWAY : PS_NEAR;
        if (hyn_66xxdata->prox_is_enable && hyn_66xxdata->prox_state != state) {
            hyn_66xxdata->prox_state = state;
            hyn_66xxdata->rp_buf.report_need |= REPORT_PROX;
        }
    }
    return ret;
}

static int cst66xx_set_workmode(enum work_mode mode, u8 enable)
{
    int ret = 0;
    ESP_LOGI(TAG, "set_workmode:%d", mode);
    hyn_66xxdata->work_mode = mode;
    if (mode != NOMAL_MODE) {
        hyn_esdcheck_switch(hyn_66xxdata, DISABLE);
    }
    if (FAC_TEST_MODE == mode) {
        cst66xx_rst();
        msleep(50);
    }
    ret = hyn_wr_reg(hyn_66xxdata, 0xD0000400, 4, 0, 0); // disable lp i2c plu
    mdelay(1);
    ret = hyn_wr_reg(hyn_66xxdata, 0xD0000400, 4, 0, 0);
    switch (mode) {
    case NOMAL_MODE:
        hyn_irq_set(hyn_66xxdata, ENABLE);
        hyn_esdcheck_switch(hyn_66xxdata, ENABLE);
        ret |= hyn_wr_reg(hyn_66xxdata, 0xD0000000, 4, 0, 0);
        ret |= hyn_wr_reg(hyn_66xxdata, 0xD0000C00, 4, 0, 0);
        ret |= hyn_wr_reg(hyn_66xxdata, 0xD0000100, 4, 0, 0);
        break;
    case GESTURE_MODE:
        ret |= hyn_wr_reg(hyn_66xxdata, 0xD0000C01, 4, 0, 0);
        break;
    case LP_MODE:
        ret |= hyn_wr_reg(hyn_66xxdata, 0xD00004AB, 4, 0, 0);
        break;
    case DIFF_MODE:
    case RAWDATA_MODE:
    case BASELINE_MODE:
    case CALIBRATE_MODE:
        ret |= hyn_wr_reg(hyn_66xxdata, 0xD00002AB, 4, 0, 0);
        ret |= hyn_wr_reg(hyn_66xxdata, 0xD00001AB, 4, 0, 0); // enter debug mode
        break;
    case FAC_TEST_MODE:
        ret |= hyn_wr_reg(hyn_66xxdata, 0xD00002AB, 4, 0, 0);
        ret |= hyn_wr_reg(hyn_66xxdata, 0xD00000AB, 4, 0, 0); // enter fac test
        break;
    case DEEPSLEEP:
        hyn_irq_set(hyn_66xxdata, DISABLE);
        ret |= hyn_wr_reg(hyn_66xxdata, 0xD00022AB, 4, 0, 0);
    default:
        ret = -2;
        break;
    }
    return ret;
}

static int cst66xx_supend(void)
{
    ESP_LOGI(TAG, "touch sleep");

    hyn_wr_reg(hyn_66xxdata, 0xD0000400, 4, 0, 0); // disable lp i2c plu
    mdelay(1);
    hyn_wr_reg(hyn_66xxdata, 0xD0000400, 4, 0, 0);

    hyn_irq_set(hyn_66xxdata, DISABLE);
    hyn_wr_reg(hyn_66xxdata, 0xD00022AB, 4, 0, 0); // DEEPSLEEP
    return 0;
}

static int cst66xx_resum(void)
{
    ESP_LOGI(TAG, "enter %s", __func__);
    cst66xx_rst();
    msleep(50);

    hyn_wr_reg(hyn_66xxdata, 0xD0000400, 4, 0, 0); // disable lp i2c plu
    mdelay(1);
    hyn_wr_reg(hyn_66xxdata, 0xD0000400, 4, 0, 0);

    hyn_irq_set(hyn_66xxdata, ENABLE);
    hyn_wr_reg(hyn_66xxdata, 0xD0000000, 4, 0, 0);
    hyn_wr_reg(hyn_66xxdata, 0xD0000C00, 4, 0, 0);
    hyn_wr_reg(hyn_66xxdata, 0xD0000100, 4, 0, 0);

    return 0;
}

static void cst66xx_rst(void)
{
    ESP_LOGI(TAG, "enter %s", __func__);
    gpio_set_value(hyn_66xxdata->plat_data.reset_gpio, 0);
    msleep(8);
    gpio_set_value(hyn_66xxdata->plat_data.reset_gpio, 1);
}

static int cst66xx_updata_tpinfo(void)
{
    u8 buf[60];
    struct tp_info *ic = &hyn_66xxdata->hw_info;
    int ret = 0;
    int retry = 5;
    while (--retry) {
        ret = 0;
        // get all config info
        ret |= cst66xx_set_workmode(NOMAL_MODE, ENABLE);
        ret |= hyn_wr_reg(hyn_66xxdata, 0xD0030000, 0x80 | 4, buf, 50);
        if (ret == 0 && buf[3] == 0xCA && buf[2] == 0xCA)
            break;
        mdelay(10);
        ret |= hyn_wr_reg(hyn_66xxdata, 0xD0000400, 4, buf, 0);
    }

    if (ret || retry == 0) {
        ESP_LOGE(TAG, "cst66xx_updata_tpinfo failed");
        return -1;
    }

    ic->fw_sensor_txnum = buf[48];
    ic->fw_sensor_rxnum = buf[49];
    ic->fw_key_num = buf[27];
    ic->fw_res_y = (buf[31] << 8) | buf[30];
    ic->fw_res_x = (buf[29] << 8) | buf[28];
    ESP_LOGI(TAG, "IC_info tx:%d rx:%d key:%d res-x:%d res-y:%d", ic->fw_sensor_txnum, ic->fw_sensor_rxnum, ic->fw_key_num,
             ic->fw_res_x, ic->fw_res_y);

    ic->fw_project_id = U8TO32(buf[39], buf[38], buf[37], buf[36]);
    ic->fw_chip_type = U8TO32(buf[3], buf[2], buf[1], buf[0]);
    ic->fw_ver = U8TO32(buf[35], buf[34], buf[33], buf[32]);
    ESP_LOGI(TAG, "IC_info fw_project_id:%04lx ictype:%04lx fw_ver:%04lx checksum:%04lx", ic->fw_project_id, ic->fw_chip_type,
             ic->fw_ver, ic->ic_fw_checksum);
    return 0;
}

const struct hyn_ts_fuc cst66xx_fuc = {
    .tp_rest = cst66xx_rst,
    .tp_report = cst66xx_report,
    .tp_supend = cst66xx_supend,
    .tp_resum = cst66xx_resum,
    .tp_chip_init = cst66xx_init,
    .tp_updata_fw = NULL,
    .tp_set_workmode = cst66xx_set_workmode,
    .tp_check_esd = NULL,
    .tp_prox_handle = NULL,
    .tp_get_dbg_data = NULL,
    .tp_get_test_result = NULL,
};
