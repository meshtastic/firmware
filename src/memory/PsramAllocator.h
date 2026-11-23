#pragma once

#include "configuration.h"
#include <memory>

#if defined(ARCH_ESP32) && defined(BOARD_HAS_PSRAM)
#include <Arduino.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <new>

// STL allocator preferring PSRAM, falling back to the internal heap. Node-based containers need it: their small
// per-node allocations sit below heap_caps_malloc_extmem_enable()'s threshold and would otherwise land in SRAM.
template <class T> struct PsramAllocator {
    using value_type = T;

    PsramAllocator() noexcept = default;
    template <class U> PsramAllocator(const PsramAllocator<U> &) noexcept {}

    T *allocate(std::size_t n)
    {
        void *p = nullptr;
        if (n <= SIZE_MAX / sizeof(T)) {
            p = ps_malloc(n * sizeof(T));
            if (!p)
                p = malloc(n * sizeof(T));
        }
        if (!p) {
#ifdef __cpp_exceptions
            throw std::bad_alloc();
#else
            abort();
#endif
        }
        return static_cast<T *>(p);
    }

    void deallocate(T *p, std::size_t) noexcept { free(p); } // IDF free() releases either region
};

template <class T, class U> bool operator==(const PsramAllocator<T> &, const PsramAllocator<U> &) noexcept
{
    return true;
}

template <class T, class U> bool operator!=(const PsramAllocator<T> &, const PsramAllocator<U> &) noexcept
{
    return false;
}
#else
// No PSRAM: exactly the default allocator, so container types are unchanged.
template <class T> using PsramAllocator = std::allocator<T>;
#endif
