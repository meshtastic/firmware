#ifndef GAME_H
#define GAME_H

#include "global.h"

namespace Game
{
  extern int16_t cameraX;
  extern uint8_t life;
  extern uint16_t timeLeft;
  extern uint16_t score;
  
  void reset();
  void play();
  void loop();

  // helper
  bool moveY(Vec& pos, int8_t dy, const Box& hitbox, bool collideToEntity = false);
}

#endif


