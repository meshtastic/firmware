#include "Arduboy2.h"

void Arduboy2Base::drawPixel(int16_t x, int16_t y, uint8_t color)
{
  if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT)
    return;
  uint8_t &cell = _buffer[x + (y >> 3) * WIDTH];
  const uint8_t bit = (uint8_t)(1u << (y & 7));
  if (color)
    cell |= bit;
  else
    cell &= (uint8_t)~bit;
}

void Arduboy2Base::fillRect(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color)
{
  for (int16_t iy = y; iy < y + h; ++iy)
    for (int16_t ix = x; ix < x + w; ++ix)
      drawPixel(ix, iy, color);
}

void Sprites::drawBitmap(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame,
                         bool masked, bool overwrite)
{
  const uint8_t width = pgm_read_byte(bitmap);
  const uint8_t height = pgm_read_byte(bitmap + 1);
  const uint8_t pages = (height + 7) / 8;
  const uint16_t bytesPerFrame = (uint16_t)width * pages * (masked ? 2 : 1);
  const uint8_t *data = bitmap + 2 + (uint16_t)frame * bytesPerFrame;

  for (uint8_t page = 0; page < pages; ++page) {
    for (uint8_t sx = 0; sx < width; ++sx) {
      uint8_t pixels;
      uint8_t mask;
      if (masked) {
        const uint16_t offset = ((uint16_t)page * width + sx) * 2;
        pixels = pgm_read_byte(data + offset);
        mask = pgm_read_byte(data + offset + 1);
      } else {
        pixels = pgm_read_byte(data + (uint16_t)page * width + sx);
        mask = overwrite ? 0xff : pixels;
      }

      for (uint8_t bit = 0; bit < 8; ++bit) {
        const int16_t sy = (int16_t)page * 8 + bit;
        if (sy >= height)
          break;
        if (mask & (1u << bit))
          ab.drawPixel(x + sx, y + sy, (pixels >> bit) & 1u);
      }
    }
  }
}

void Sprites::drawOverwrite(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame)
{
  drawBitmap(x, y, bitmap, frame, false, true);
}

void Sprites::drawSelfMasked(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame)
{
  drawBitmap(x, y, bitmap, frame, false, false);
}

void Sprites::drawPlusMask(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame)
{
  drawBitmap(x, y, bitmap, frame, true, false);
}

