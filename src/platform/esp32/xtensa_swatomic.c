// Weak software __atomic_*_{1,2,4} for ESP32-S2/S3. -mdisable-hardware-atomics makes
// GCC emit these libcalls, but the precompiled libnewlib ships only the _8 variants and
// some toolchains' libgcc ships none, so S3 links fail on macOS. Weak = a real libgcc
// definition wins with no clash. Spinlock/critical-section like IDF's stdatomic.c.

#include "sdkconfig.h"

#if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32S2)

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"

// These names are GCC builtins; declaring them trips -Wbuiltin-declaration-mismatch
// (uint32_t vs GCC's unsigned int, same ABI). Suppressed as IDF's stdatomic.c does.
#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"

static portMUX_TYPE s_swatomic_mux = portMUX_INITIALIZER_UNLOCKED;

#define SWATOMIC_ENTER() portENTER_CRITICAL_SAFE(&s_swatomic_mux)
#define SWATOMIC_EXIT() portEXIT_CRITICAL_SAFE(&s_swatomic_mux)

// load / store / exchange / compare_exchange for one width.
#define GEN_SWATOMIC_CORE(N, TYPE)                                                                                               \
    __attribute__((weak, used)) TYPE __atomic_load_##N(const volatile void *ptr, int memorder)                                   \
    {                                                                                                                            \
        (void)memorder;                                                                                                          \
        SWATOMIC_ENTER();                                                                                                        \
        TYPE ret = *(const volatile TYPE *)ptr;                                                                                  \
        SWATOMIC_EXIT();                                                                                                         \
        return ret;                                                                                                              \
    }                                                                                                                            \
    __attribute__((weak, used)) void __atomic_store_##N(volatile void *ptr, TYPE val, int memorder)                              \
    {                                                                                                                            \
        (void)memorder;                                                                                                          \
        SWATOMIC_ENTER();                                                                                                        \
        *(volatile TYPE *)ptr = val;                                                                                             \
        SWATOMIC_EXIT();                                                                                                         \
    }                                                                                                                            \
    __attribute__((weak, used)) TYPE __atomic_exchange_##N(volatile void *ptr, TYPE val, int memorder)                           \
    {                                                                                                                            \
        (void)memorder;                                                                                                          \
        SWATOMIC_ENTER();                                                                                                        \
        TYPE old = *(volatile TYPE *)ptr;                                                                                        \
        *(volatile TYPE *)ptr = val;                                                                                             \
        SWATOMIC_EXIT();                                                                                                         \
        return old;                                                                                                              \
    }                                                                                                                            \
    __attribute__((weak, used)) bool __atomic_compare_exchange_##N(volatile void *ptr, void *expected, TYPE desired,             \
                                                                   bool is_weak, int success, int failure)                       \
    {                                                                                                                            \
        (void)is_weak;                                                                                                           \
        (void)success;                                                                                                           \
        (void)failure;                                                                                                           \
        bool ok;                                                                                                                 \
        SWATOMIC_ENTER();                                                                                                        \
        TYPE cur = *(volatile TYPE *)ptr;                                                                                        \
        if (cur == *(TYPE *)expected) {                                                                                          \
            *(volatile TYPE *)ptr = desired;                                                                                     \
            ok = true;                                                                                                           \
        } else {                                                                                                                 \
            *(TYPE *)expected = cur;                                                                                             \
            ok = false;                                                                                                          \
        }                                                                                                                        \
        SWATOMIC_EXIT();                                                                                                         \
        return ok;                                                                                                               \
    }

// A read-modify-write pair: fetch_<name> returns the old value, <name>_fetch the
// new. EXPR is evaluated over `old` and `val`.
#define GEN_SWATOMIC_RMW(N, TYPE, NAME, EXPR)                                                                                    \
    __attribute__((weak, used)) TYPE __atomic_fetch_##NAME##_##N(volatile void *ptr, TYPE val, int memorder)                     \
    {                                                                                                                            \
        (void)memorder;                                                                                                          \
        SWATOMIC_ENTER();                                                                                                        \
        TYPE old = *(volatile TYPE *)ptr;                                                                                        \
        *(volatile TYPE *)ptr = (TYPE)(EXPR);                                                                                    \
        SWATOMIC_EXIT();                                                                                                         \
        return old;                                                                                                              \
    }                                                                                                                            \
    __attribute__((weak, used)) TYPE __atomic_##NAME##_fetch_##N(volatile void *ptr, TYPE val, int memorder)                     \
    {                                                                                                                            \
        (void)memorder;                                                                                                          \
        SWATOMIC_ENTER();                                                                                                        \
        TYPE old = *(volatile TYPE *)ptr;                                                                                        \
        TYPE nv = (TYPE)(EXPR);                                                                                                  \
        *(volatile TYPE *)ptr = nv;                                                                                              \
        SWATOMIC_EXIT();                                                                                                         \
        return nv;                                                                                                               \
    }

#define GEN_SWATOMIC_ALL(N, TYPE)                                                                                                \
    GEN_SWATOMIC_CORE(N, TYPE)                                                                                                   \
    GEN_SWATOMIC_RMW(N, TYPE, add, old + val)                                                                                    \
    GEN_SWATOMIC_RMW(N, TYPE, sub, old - val)                                                                                    \
    GEN_SWATOMIC_RMW(N, TYPE, and, old &val)                                                                                     \
    GEN_SWATOMIC_RMW(N, TYPE, or, old | val)                                                                                     \
    GEN_SWATOMIC_RMW(N, TYPE, xor, old ^ val)                                                                                    \
    GEN_SWATOMIC_RMW(N, TYPE, nand, ~(old & val))

GEN_SWATOMIC_ALL(1, uint8_t)
GEN_SWATOMIC_ALL(2, uint16_t)
GEN_SWATOMIC_ALL(4, uint32_t)

#else

// Keep this a non-empty translation unit on targets that inline hardware atomics.
typedef int swatomic_translation_unit_not_empty;

#endif // CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32S2
