#ifndef MAP_H
#define MAP_H

#include "global.h"
#include "entity.h"

namespace Map
{
extern uint8_t width;
extern bool showBackground;
extern Entity* boss;

void init(const uint8_t* source);
bool collide(int16_t x, int8_t y, const Box& hitbox);
void draw();
}

#endif


