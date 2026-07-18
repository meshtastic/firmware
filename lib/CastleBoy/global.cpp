#include "global.h"

#include "assets.h"

Arduboy2Base ab;
Sprites sprites;
static bool audioEnabled()
{
  return ab.audio.enabled();
}

ArduboyTones sound(audioEnabled);
uint8_t mainState;
uint8_t flashCounter = 0;

void Util::toggle(uint8_t & flags, uint8_t mask)
{
  if (flags & mask)
  {
    flags &= ~mask;
  }
  else
  {
    flags |= mask;
  }
}

bool Util::collideRect(int16_t x1, int8_t y1, uint8_t width1, uint8_t height1, int16_t x2, int8_t y2, uint8_t width2, uint8_t height2)
{

  return !(x1            >= x2 + width2  ||
           x1 + width1   <= x2           ||
           y1            >= y2 + height2 ||
           y1 + height1  <= y2);
}


// Inspired by TEAMArg's Sirene, stages.h:775
// But optimized (use of int8_t, use cast instead of for loop)
// Also use alignment (LEFT, RIGHT, CENTER) instead of zero padding
void Util::drawNumber(int16_t x, int16_t y, uint16_t value, uint8_t align)
{
  char buf[10];
  ltoa(value, buf, 10);
  uint8_t strLength = strlen(buf);
  int8_t offset;
  switch (align)
  {
    case ALIGN_LEFT:
      offset = 0;
      break;
    case ALIGN_CENTER:
      offset = -(strLength * 2);
      break;
    case ALIGN_RIGHT:
      offset = -(strLength * 4);
      break;
  }

  // draw the frame
  ab.fillRect(x + offset - 1, y, 4 * strLength + 1, 7, BLACK);

  // draw the number
  for (uint8_t i = 0; i < strLength; i++)
  {
    uint8_t digit = (uint8_t) buf[i];
    digit -= 48;
    if (digit > 9) digit = 0;
    sprites.drawSelfMasked(x + offset + 4 * i, y, font, digit);
  }
}

int freeRam() { return 0; }

#ifdef DEBUG_LOG
#include "menu.h"
int16_t debugValue = 0;
void drawDebugLog()
{
  Util::drawNumber(0, 0, debugValue, ALIGN_LEFT);
}
#endif

#ifdef DEBUG_CPU
#include "menu.h"
void drawDebugCpu()
{
  Util::drawNumber(128, 0, ab.cpuLoad(), ALIGN_RIGHT);
}
#endif

#ifdef DEBUG_RAM
#include "menu.h"
void drawDebugRam()
{
  extern int __heap_start, *__brkval;
  int v;
  int ram = (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);

  Util::drawNumber(64, 0, ram, ALIGN_CENTER);
}
#endif
