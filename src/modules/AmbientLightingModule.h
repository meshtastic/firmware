#pragma once

#include "main.h"

#ifdef HAS_NCP5623
#include <NCP5623.h>
extern NCP5623 rgb;

#endif

class AmbientLightingModule
{
  public:
    AmbientLightingModule(); // default constructor
};