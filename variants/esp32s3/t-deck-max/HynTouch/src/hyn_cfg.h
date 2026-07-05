
#ifndef _HYNITRON_CFG_H
#define _HYNITRON_CFG_H

#ifdef __cplusplus
extern "C" {
#endif

/**delay ms */
#define mdelay(ms) hyn_delay_ms(ms)
#define msleep(ms) hyn_delay_ms(ms)

/**function config */
#define HYN_POWER_ON_UPDATA (0) // touch fw updata

#define MAX_POINTS_REPORT (5) // max report point

#define HYN_DRIVER_VERSION "== Hynitron V2.10 20241111 =="

#ifdef __cplusplus
}
#endif
#endif
