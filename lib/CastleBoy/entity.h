#ifndef CANDLE_H
#define CANDLE_H

#include "global.h"

struct Entity
{
  uint8_t type;
  Vec pos;
  uint8_t hp;

  // xxxxxxxx
  // ||||||||
  // |||||||+-|
  // ||||||+--| hurt counter 
  // |||||+---|
  // ||||+----|
  // |||+----- misc2
  // ||+------ misc1
  // |+------- alive
  // +-------- present
  uint8_t state;
  
  uint8_t frame;
  uint8_t counter;
};

namespace Entities
{
  void init();
  Entity* add(uint8_t type, int16_t x, int8_t y);
  void update();
  bool damage(int16_t x, int8_t y, uint8_t width, uint8_t height, uint8_t value);
  bool moveCollide(int16_t x, int8_t y, int8_t offsetX, int8_t offsetY, const Box& hitbox);
  Entity* checkPlayer(int16_t x, int8_t y, uint8_t width, uint8_t height);
  void draw();
}


#endif
