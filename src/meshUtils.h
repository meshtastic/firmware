#pragma once

/// C++ v17+ clamp function, limits a given value to a range defined by lo and hi
template <class T> constexpr const T &clamp(const T &v, const T &lo, const T &hi)
{
    return (v < lo) ? lo : (hi < v) ? hi : v;
}

#if (defined(ARCH_PORTDUINO) && !defined(STRNSTR))
#define STRNSTR
#include <string.h>
char *strnstr(const char *s, const char *find, size_t slen);
#endif