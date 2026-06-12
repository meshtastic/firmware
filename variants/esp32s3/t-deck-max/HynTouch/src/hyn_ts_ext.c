
#include "hyn_core.h"

void hyn_irq_set(struct hyn_ts_data *ts_data, u8 value)
{
    // HYN_ENTER();
}

void hyn_set_i2c_addr(struct hyn_ts_data *ts_data, u8 addr)
{
    ts_data->salve_addr = addr;
}

u16 hyn_sum16(int val, u8 *buf, u16 len)
{
    u16 sum = val;
    while (len--)
        sum += *buf++;
    return sum;
}

u32 hyn_sum32(int val, u32 *buf, u16 len)
{
    u32 sum = val;
    while (len--)
        sum += *buf++;
    return sum;
}

// int hyn_u8_extend_u16(u8 *des_ptr, u16 len)
// {
// 	u8 *src_ptr = des_ptr+len;
// 	while(--len){
// 		*des_ptr++ = *src_ptr++;
// 		*des_ptr++ = 0;
// 	}
// }

void hyn_esdcheck_switch(struct hyn_ts_data *ts_data, u8 enable) {}

int copy_for_updata(struct hyn_ts_data *ts_data, u8 *buf, u32 offset, u16 len)
{
    int ret = -1;

    return ret;
}

int hyn_wait_irq_timeout(struct hyn_ts_data *ts_data, int msec)
{
    ts_data->hyn_irq_flg = 0;
    while (msec--) {
        msleep(1);
        if (ts_data->hyn_irq_flg == 1) {
            ts_data->hyn_irq_flg = 0;
            msec = -1;
            break;
        }
    }
    return msec == -1 ? 0 : -1;
}

static int hyn_get_threshold(char *filename, char *match_string, s16 *pstore, u16 len)
{
    return -1;
}

int factory_multitest(struct hyn_ts_data *ts_data, char *cfg_path, u8 *data, s16 *test_th, u8 test_item)
{
    return -1;
}

int fac_test_log_save(char *log_name, struct hyn_ts_data *ts_data, s16 *test_data, int test_ret, u8 test_item)
{
    return 0;
}
