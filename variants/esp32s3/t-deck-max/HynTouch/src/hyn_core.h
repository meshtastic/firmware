#ifndef HYNITRON_CORE_H
#define HYNITRON_CORE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "hyn_cfg.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define HYN_INFO(fmt, args...)  // printf("[HYN]"fmt"\n", ##args)
#define HYN_INFO2(fmt, args...) // if(hyn_data->log_level > 0)printf("[HYN]"fmt"\n", ##args)
#define HYN_INFO3(fmt, args...) // if(hyn_data->log_level > 1)printf("[HYN]"fmt"\n", ##args)
#define HYN_INFO4(fmt, args...) // if(hyn_data->log_level > 2)printf("[HYN]"fmt"\n", ##args)
#define HYN_ERROR(fmt, args...) // printf("[HYN][Error]%s:"fmt"\n",__func__,##args)
#define HYN_ENTER()             // printf("[HYN][enter]%s\n",__func__)

// #define IS_ERR_OR_NULL(x)  (x <= 0)
#define U8TO16(x1, x2) ((((x1)&0xFF) << 8) | ((x2)&0xFF))
#define U8TO32(x1, x2, x3, x4) ((((x1)&0xFF) << 24) | (((x2)&0xFF) << 16) | (((x3)&0xFF) << 8) | ((x4)&0xFF))
#define U16REV(x) ((((x) << 8) & 0xFF00) | (((x) >> 8) & 0x00FF))

// #undef bool
// #undef NULL
#undef FALSE
#undef TRUE
#undef DISABLE
#undef ENABLE
// #define NULL  ((void*)0)
#define FALSE (-1)
#define TRUE (0)
#define DISABLE (0)
#define ENABLE (1)

#ifndef HIGH
#define HIGH (1)
#endif

#define PS_FAR_AWAY 1
#define PS_NEAR 0

#define MULTI_OPEN_TEST (0x80)
#define MULTI_SHORT_TEST (0x01)
#define MULTI_SCAP_TEST (0x02)

typedef uint8_t u8;
// typedef uint8_t  bool;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;

enum work_mode {
    NOMAL_MODE = 0,
    GESTURE_MODE = 1,
    LP_MODE = 2,
    DEEPSLEEP = 3,
    DIFF_MODE = 4,
    RAWDATA_MODE = 5,
    BASELINE_MODE = 6,
    CALIBRATE_MODE = 7,
    FAC_TEST_MODE = 8,
    ENTER_BOOT_MODE = 0xCA,
};

enum report_typ { REPORT_NONE = 0, REPORT_POS = 0x01, REPORT_KEY = 0x02, REPORT_GES = 0x04, REPORT_PROX = 0x08 };

enum fac_test_ero {
    FAC_TEST_PASS = 0,
    FAC_GET_DATA_FAIL = -1,
    FAC_GET_CFG_FAIL = -4,
    FAC_TEST_OPENL_FAIL = -5,
    FAC_TEST_OPENH_FAIL = -7,
    FAC_TEST_SHORT_FAIL = -6,
    FAC_TEST_SCAP_FAIL = -8,
};

enum ges_idx {
    IDX_U = 0,
    IDX_UP,
    IDX_DOWN,
    IDX_LEFT,
    IDX_RIGHT,
    IDX_O,
    IDX_e,
    IDX_M,
    IDX_L,
    IDX_W,
    IDX_S,
    IDX_V,
    IDX_C,
    IDX_Z,
    IDX_POWER,
    IDX_NULL = 0xFF,
};

struct hyn_plat_data {
    int reset_gpio;
    int irq_gpio;

    u32 x_resolution;
    u32 y_resolution;
    int swap_xy;
    int reverse_x;
    int reverse_y;

    int max_touch_num;
    int key_num;
    u32 key_x_coords[8]; // max support 8 keys
    u32 key_y_coords;
    u32 key_code[8];
};

struct hyn_chip_series {
    u32 part_no;
    u32 moudle_id;
    u8 chip_name[20];
    u8 *fw_bin;
};

struct ts_frame {
    u8 rep_num;
    enum report_typ report_need;
    u8 key_id;
    u8 key_state;
    struct {
        u8 pos_id;
        u8 event;
        u16 pos_x;
        u16 pos_y;
        u16 pres_z;
    } pos_info[MAX_POINTS_REPORT];
};

struct tp_info {
    u8 fw_sensor_txnum;
    u8 fw_sensor_rxnum;
    u8 fw_key_num;
    u8 reserve;
    u16 fw_res_y;
    u16 fw_res_x;
    u32 fw_boot_time;
    u32 fw_project_id;
    u32 fw_chip_type;
    u32 fw_ver;
    u32 ic_fw_checksum;
    u32 fw_module_id;
};

struct hyn_ts_data {
    u16 bus_type;
    u8 salve_addr;
    int gpio_irq;
    int esd_fail_cnt;
    u32 esd_last_value;
    enum work_mode work_mode;

    int power_is_on;
    u8 hyn_irq_flg;
    struct hyn_plat_data plat_data;
    struct tp_info hw_info;
    struct ts_frame rp_buf;

    int boot_is_pass;
    int need_updata_fw;
    u8 fw_file_name[128];
    u8 *fw_updata_addr;
    int fw_updata_len;
    int fw_dump_state;
    u8 fw_updata_process;
    u8 host_cmd_save[16];

    u8 log_level;
    u8 prox_is_enable;
    u8 prox_state;

    u8 gesture_is_enable;
    u8 gesture_id;
    const struct hyn_ts_fuc *hyn_fuc_used;
};

struct hyn_ts_fuc {
    void (*tp_rest)(void);
    int (*tp_report)(void);
    int (*tp_supend)(void);
    int (*tp_resum)(void);
    int (*tp_chip_init)(struct hyn_ts_data *ts_data);
    int (*tp_updata_fw)(u8 *bin_addr, u16 len);
    int (*tp_set_workmode)(enum work_mode mode, u8 enable);
    u32 (*tp_check_esd)(void);
    int (*tp_prox_handle)(u8 cmd);
    int (*tp_get_dbg_data)(u8 *buf, u16 len);
    int (*tp_get_test_result)(u8 *buf, u16 len);
};

// hyn_i2c.c
esp_err_t hyn_i2c_init(u8 pin_sda, u8 pin_scl);
int hyn_write_data(struct hyn_ts_data *ts_data, u8 *buf, u8 reg_len, u16 len);
int hyn_read_data(struct hyn_ts_data *ts_data, u8 *buf, u16 len);
int hyn_wr_reg(struct hyn_ts_data *ts_data, u32 reg_addr, u8 reg_len, u8 *rbuf, u16 rlen);
int gpio_set_value(uint32_t gpio_id, bool vlue);
bool gpio_get_value(uint32_t gpio_id);
void hyn_delay_ms(int cnt);

// hyn_ts_ext.c
void hyn_irq_set(struct hyn_ts_data *ts_data, u8 value);
void hyn_set_i2c_addr(struct hyn_ts_data *ts_data, u8 addr);
u16 hyn_sum16(int val, u8 *buf, u16 len);
u32 hyn_sum32(int val, u32 *buf, u16 len);
void hyn_esdcheck_switch(struct hyn_ts_data *ts_data, u8 enable);
int copy_for_updata(struct hyn_ts_data *ts_data, u8 *buf, u32 offset, u16 len);
int hyn_wait_irq_timeout(struct hyn_ts_data *ts_data, int msec);
int factory_multitest(struct hyn_ts_data *ts_data, char *cfg_path, u8 *data, s16 *test_th, u8 test_item);
int fac_test_log_save(char *log_name, struct hyn_ts_data *ts_data, s16 *test_data, int test_ret, u8 test_item);

// ic type
extern const struct hyn_ts_fuc cst1xx_fuc;
extern const struct hyn_ts_fuc cst3xx_fuc;
extern const struct hyn_ts_fuc cst66xx_fuc;
extern const struct hyn_ts_fuc cst7xx_fuc;
extern const struct hyn_ts_fuc cst8xxT_fuc;
extern const struct hyn_ts_fuc cst92xx_fuc;
extern const struct hyn_ts_fuc cst3240_fuc;
extern const struct hyn_ts_fuc cst226se_fuc;

#ifdef __cplusplus
}
#endif
#endif
