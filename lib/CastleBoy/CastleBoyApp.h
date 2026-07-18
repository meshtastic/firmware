#pragma once

#include <stdint.h>

namespace CastleBoyApp {
  enum Button : uint8_t {
    Left  = 1u << 0,
    Right = 1u << 1,
    Up    = 1u << 2,
    Down  = 1u << 3,
    A     = 1u << 4,
    B     = 1u << 5,
  };

  void begin();
  void step(uint8_t buttons);
  const uint8_t *buffer();
  const uint8_t *xbmBuffer();
}
