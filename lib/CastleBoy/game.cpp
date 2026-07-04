#include "game.h"

#include "menu.h"
#include "map.h"
#include "player.h"
#include "entity.h"
#include "assets.h"

int16_t Game::cameraX;
uint8_t Game::life;
uint16_t Game::timeLeft;
uint16_t Game::score;

namespace
{
const uint8_t* const levels[] = { stage_1_1, stage_1_2, stage_1_3, stage_1_4, stage_2_1, stage_2_2, stage_2_3, stage_2_4, stage_3_1, stage_3_2, stage_3_3, stage_3_4 };

bool paused;
bool finished;
uint8_t levelIndex;
uint8_t pauseCounter;

void drawHpBar(int16_t x, int16_t y, uint8_t value, uint8_t max)
{
  ab.fillRect(x, y, 4 * max, 7, BLACK);
  for (uint8_t i = 0; i < max; i++)
  {
    sprites.drawSelfMasked(x + i * 4, y, i < value ? ui_hp_full : ui_hp_empty, 0);
  }
}

}

void Game::reset()
{
  levelIndex = 0;
  life = GAME_STARTING_LIFE;
  score = 0;
  Player::hp = PLAYER_MAX_HP;
  Player::knifeCount = 0;
}

void Game::play()
{
  paused = false;
  finished = false;
  mainState = STATE_PLAY;
  Entities::init();
  Map::init(levels[levelIndex]);
  cameraX = 0;
  pauseCounter = 0;

  if (Map::boss != NULL)
  {
    sound.tone(NOTE_G4, 300, NOTE_G3, 300, NOTE_G2, 900);
    pauseCounter = 120;
  }
}

void Game::loop()
{
  // debug
#ifdef DEBUG_CHEAT
//  if (ab.pressed(A_BUTTON) && ab.pressed(B_BUTTON) && ab.justPressed(DOWN_BUTTON))
//  {
//    play();
//    return;
//  }

  if (ab.pressed(A_BUTTON) && ab.pressed(B_BUTTON) && ab.justPressed(UP_BUTTON))
  {
    if (Map::boss != NULL)
    {
      Map::boss->state &= ~0x40; // FLAG_ALIVE in entity.cpp
      Map::boss->hp = 0;
      Map::boss->counter = 0;
    }
    else
    {
      ++levelIndex;
      play();
    }
    return;
  }

//  if (ab.pressed(A_BUTTON) && ab.pressed(B_BUTTON) && ab.justPressed(LEFT_BUTTON))
//  {
//    finished = false;
//    mainState = STATE_PLAY;
//    Entities::init();
//    Map::init(stage_test);
//    cameraX = 0;
//    return;
//  }
#endif

  // pause
  if (paused)
  {
    sprites.drawOverwrite((WIDTH / 2) - pgm_read_byte(text_paused) / 2, HEIGHT / 2, text_paused, 0);
    if (ab.justPressed(B_BUTTON) || ab.justPressed(A_BUTTON))
    {
      paused = false;
    }
    return;
  }

  if (ab.pressed(DOWN_BUTTON) && ab.justPressed(A_BUTTON))
  {
    paused = true;
    return;
  }

  // update
  if (pauseCounter > 0)
  {
    pauseCounter--;
  }
  else
  {
    Player::update();
    Entities::update();
  }

  if (!finished)
  {
    if (timeLeft > 0)
    {
      --timeLeft;
    }

    // finished: exit from left
    if (Player::pos.x - 4 /*normalHitbox.x*/ > Map::width * TILE_WIDTH)
    {
      ++levelIndex;
      Menu::notifyLevelFinished();
      finished = true;
    }
    // finished: boss killed
    else if (Map::boss != NULL && Map::boss->hp == 0)
    {
      ++levelIndex;
      Menu::notifyLevelFinished();
      finished = true;
    }

    // finished: check if player is alive
    else if (!Player::alive || timeLeft == 0)
    {
      Player::alive = false;
      Player::knifeCount = 0;
      if (timeLeft == 0)
      {
        life = 0;
      }
      else
      {
        timeLeft += GAME_EXTRA_TIME;
        --life;
      }
      Menu::notifyPlayerDied();
      finished = true;
    }
  }

  // update camera
  if (Player::pos.x < cameraX + CAMERA_LEFT_BUFFER)
  {
    cameraX = Player::pos.x - CAMERA_LEFT_BUFFER;
    if (cameraX < 0) cameraX = 0;
  }
  else if (Player::pos.x > cameraX + 128 - CAMERA_RIGHT_BUFFER)
  {
    cameraX = Player::pos.x - 128 + CAMERA_RIGHT_BUFFER;
    if (cameraX > Map::width * TILE_WIDTH - 128) cameraX = Map::width * TILE_WIDTH - 128;
  }

  // draw: parralax
  if (Map::showBackground)
  {
    int16_t backgroundOffset = cameraX / 28; // FIXME properly calculate parralax unless all maps have same width
    sprites.drawOverwrite(16 - backgroundOffset, 4, background_mountain, 0);
  }

  // draw: main
  Map::draw();
  Entities::draw();
  Player::draw();

  // ui: hp
  drawHpBar(0, 0, Player::hp, PLAYER_MAX_HP);

  // ui: knife count
  ab.fillRect(54, 0, 13, 7, BLACK);
  sprites.drawSelfMasked(55, 0, ui_knife_count, 0);

  Util::drawNumber(68, 0, Player::knifeCount, ALIGN_LEFT);

  // ui: time left
  Util::drawNumber(128, 0, timeLeft / FPS, ALIGN_RIGHT);

  // ui: boss
  if (Map::boss != NULL)
  {
    drawHpBar(40, 58, Map::boss->hp, BOSS_MAX_HP);
  }
}

bool Game::moveY(Vec& pos, int8_t dy, const Box& hitbox, bool collideToEntity)
{
  int8_t sign = dy > 0 ? 1 : -1;
  while (dy != 0)
  {
    if (Map::collide(pos.x, pos.y + sign, hitbox) || (collideToEntity && Entities::moveCollide(pos.x, pos.y, 0, sign, hitbox)))
    {
      return true;
    }
    pos.y += sign;
    dy -= sign;
  }

  return false;
}
