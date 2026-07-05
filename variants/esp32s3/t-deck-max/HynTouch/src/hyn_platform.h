#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Platform hook for the HYN touch stack.
// Returns 1 if handled, 0 if the caller should fall back to a normal GPIO,
// or -1 if the GPIO is virtual but no handler is available.
int hyn_platform_gpio_set_value(uint32_t gpio_id, int value);
int hyn_platform_gpio_get_value(uint32_t gpio_id, int *out_value);

#ifdef __cplusplus
}
#endif
