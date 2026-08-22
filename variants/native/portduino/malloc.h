#pragma once

#if defined(__APPLE__)
#include <errno.h>
#include <malloc/malloc.h>
#include <stdlib.h>

// LovyanGFX includes the Linux header name; bridge it for Darwin native builds.
static inline void *memalign(size_t alignment, size_t size)
{
    void *ptr = NULL;
    int ret = posix_memalign(&ptr, alignment, size);
    if (ret != 0) {
        errno = ret;
        return NULL;
    }
    return ptr;
}
#else
#include_next <malloc.h>
#endif
