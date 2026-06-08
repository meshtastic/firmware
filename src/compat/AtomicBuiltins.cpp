#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"

// Some ESP32-S3 toolchain/platform combinations reference GCC atomic runtime
// helpers but do not ship a separate libatomic. Provide minimal 32-bit
// implementations used by std::atomic/shared_ptr internals.
//
// These functions are intentionally small and protected by a FreeRTOS critical
// section to keep operations atomic on the target MCU.

static portMUX_TYPE atomicBuiltinsMux = portMUX_INITIALIZER_UNLOCKED;

extern "C" unsigned int __atomic_fetch_add_4(volatile void *ptr, unsigned int val, int memmodel)
{
    (void)memmodel;

    portENTER_CRITICAL(&atomicBuiltinsMux);
    volatile unsigned int *p = static_cast<volatile unsigned int *>(ptr);
    unsigned int old = *p;
    *p = old + val;
    portEXIT_CRITICAL(&atomicBuiltinsMux);

    return old;
}

extern "C" unsigned int __atomic_fetch_sub_4(volatile void *ptr, unsigned int val, int memmodel)
{
    (void)memmodel;

    portENTER_CRITICAL(&atomicBuiltinsMux);
    volatile unsigned int *p = static_cast<volatile unsigned int *>(ptr);
    unsigned int old = *p;
    *p = old - val;
    portEXIT_CRITICAL(&atomicBuiltinsMux);

    return old;
}

extern "C" unsigned int __atomic_exchange_4(volatile void *ptr, unsigned int val, int memmodel)
{
    (void)memmodel;

    portENTER_CRITICAL(&atomicBuiltinsMux);
    volatile unsigned int *p = static_cast<volatile unsigned int *>(ptr);
    unsigned int old = *p;
    *p = val;
    portEXIT_CRITICAL(&atomicBuiltinsMux);

    return old;
}