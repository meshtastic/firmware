#include "configuration.h"
#include "rtos.h"
#include <assert.h>
#include <stdlib.h>

/**
 * Custom new/delete to panic if out out memory
 */

void *operator new(size_t size)
{
    auto p = rtos_malloc(size);
    assert(p);
    return p;
}

void *operator new[](size_t size)
{
    auto p = rtos_malloc(size);
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