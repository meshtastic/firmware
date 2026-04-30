#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_attr.h"

#ifdef ESP32_FORCE_IRAM_MEMSET

/*
 * T-Beam/classic ESP32 boot workaround
 * ------------------------------------
 * During early flash operations the ESP32 disables cache, but some IRAM flash
 * code paths still reach libc memcpy/memset. If those resolve to flash-resident
 * implementations, startup can panic with cache-disabled access errors.
 *
 * We wrap memcpy/memset for the T-Beam environment. Fast path uses the
 * normal libc routines when cache is enabled; slow path uses IRAM-safe byte
 * loops when cache is disabled.
 */

extern void *__real_memcpy(void *dst, const void *src, size_t n);

static inline bool IRAM_ATTR cache_is_enabled(void)
{
    return (*(volatile uint32_t *)0x3FF00040u) & (1u << 3);
}

extern void *IRAM_ATTR __wrap_memcpy(void *dst, const void *src, size_t n)
{
    if (cache_is_enabled()) {
        return __real_memcpy(dst, src, n);
    }

    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dst;
}

#endif
