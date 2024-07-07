#pragma once
#include <cstddef>
#include <cstdint>

class Throttle
{
  public:
    static bool execute(uint32_t *lastExecutionMs, uint32_t minumumIntervalMs, void (*func)(void), void (*onDefer)(void) = NULL);
};