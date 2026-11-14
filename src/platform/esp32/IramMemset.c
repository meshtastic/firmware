#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_attr.h"
#include "sdkconfig.h"

#ifdef ESP32_FORCE_IRAM_MEMSET

#if !defined(CONFIG_IDF_TARGET_ESP32)
#error "ESP32_FORCE_IRAM_MEMSET is classic-ESP32 only: cache_is_enabled() reads a DPORT register that other targets do not map."
#endif

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
 *
 * Classic ESP32 only: the probe below reads DPORT_PRO_CACHE_CTRL_REG, an address
 * no other target maps, so the guard above keeps this off the S3/C3/C6.
 */

extern void *__real_memset(void *dst, int c, size_t n);

static inline bool IRAM_ATTR cache_is_enabled(void)
{
    return (*(volatile uint32_t *)0x3FF00040u) & (1u << 3);
}

extern void *IRAM_ATTR __wrap_memset(void *dst, int c, size_t n)
{
    if (cache_is_enabled()) {
        return __real_memset(dst, c, n);
    }

    uint8_t *ptr = (uint8_t *)dst;
    const uint8_t fill = (uint8_t)c;
    while (n--) {
        *ptr++ = fill;
    }
    return dst;
}

#endif