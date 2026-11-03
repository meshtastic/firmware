#include "configuration.h"
#include "rtos.h"
#include <assert.h>
#include <stdlib.h>

/**
 * Custom new/delete to panic if out out memory
 */

// The assert below can only name this file, so log the request and who made it first
static void __attribute__((noinline)) logAllocFailure(size_t size, void *caller)
{
    LOG_ERROR("new(%u) failed, caller 0x%08lx", (unsigned)size, (unsigned long)caller);
}

void *__attribute__((noinline)) operator new(size_t size)
{
    auto p = rtos_malloc(size);
    if (!p)
        logAllocFailure(size, __builtin_return_address(0));
    assert(p);
    return p;
}

void *__attribute__((noinline)) operator new[](size_t size)
{
    auto p = rtos_malloc(size);
    if (!p)
        logAllocFailure(size, __builtin_return_address(0));
    assert(p);
    return p;
}

void operator delete(void *ptr)
{
    rtos_free(ptr);
}

void operator delete[](void *ptr)
{
    rtos_free(ptr);
}