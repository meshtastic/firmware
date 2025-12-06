#pragma once
#ifndef _MT_MEMGET_H
#define _MT_MEMGET_H

#include <Arduino.h>

class MemGet
{
  public:
    uint32_t getFreeHeap();
    uint32_t getHeapSize();
    uint32_t getFreePsram();
    uint32_t getPsramSize();
};

extern MemGet memGet;

#endif
