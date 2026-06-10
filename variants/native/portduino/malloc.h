#pragma once

#if defined(__APPLE__)
#include <malloc/malloc.h>
#include <stdlib.h>

// LovyanGFX includes the Linux header name; bridge it for Darwin native builds.
static inline void *memalign(size_t alignment, size_t size)
{
    void *ptr = NULL;
    return posix_memalign(&ptr, alignment, size) == 0 ? ptr : NULL;
}
#else
#include_next <malloc.h>
#endif
