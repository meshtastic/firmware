#ifndef PLAYER_H
#define PLAYER_H

#include "global.h"

namespace Player
{
extern Vec pos;
extern uint8_t hp;
extern bool alive;
extern uint8_t knifeCount;
  
void init(int16_t x, int8_t y);
void update();
void draw();
}

#endif


