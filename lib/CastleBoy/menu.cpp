#include "menu.h"

#include "game.h"
#include "player.h"
#include "map.h"
#include "assets.h"

#define TITLE_OPTION_MAX 2
#define TITLE_OPTION_PLAY 0
#define TITLE_OPTION_HELP 1
#define TITLE_OPTION_SFX 2

namespace
{
uint8_t stage;
uint8_t counter;
uint8_t state;
bool toggle = 0;
uint8_t menuIndex;
int8_t offset;

#define B_X 70
#define B_XX 140
#define B_XXXX 280

const uint16_t beat_game_finished[] PROGMEM = {
  // 1:1 ----------------------------------
  NOTE_C5,  B_X,
  NOTE_C4,  B_X,
  0,        B_XX,
  NOTE_C2,  B_XXXX,

  // 1:2 -------------
  0,        B_XX,
  NOTE_C2,  B_XXXX,
  0,        B_XX,

  // 1:3 -------------
  NOTE_C2,  B_XXXX,
  0,        B_XX,
  0,        B_XX,

  // 1:4 -------------
  0,        B_XX,
  0,        B_XX,
  NOTE_C4,  B_XXXX,

  // 2:1 ----------------------------------
  NOTE_C4,  B_X,
  NOTE_C4,        B_X,
  NOTE_C2,  B_XXXX,
  0,        B_XX,

  // 2:2 -------------
  NOTE_C3,  B_XX,
  0,        B_XX,
  NOTE_C2,  B_XXXX,

  // 2:3 -------------
  0,        B_XX,
  NOTE_C2,  B_XXXX,
  0,        B_XX,

  // 3:4 -------------
  0,        B_XX,
  0,        B_XX,
  NOTE_C2,  B_XXXX,

  TONES_REPEAT
};
}

void Menu::showTitle()
{
  mainState = STATE_TITLE;
  state = 0;
  counter = 60;
  menuIndex = TITLE_OPTION_PLAY;
  stage = 1;
  Player::hp = PLAYER_MAX_HP;
  Game::reset();
}

void Menu::notifyPlayerDied()
{
  mainState = STATE_PLAYER_DIED;
  counter = 140;
  sound.tone(NOTE_G3, 100, NOTE_G2, 150, NOTE_G1, 350);
}

void Menu::notifyLevelFinished()
{
  if (Map::boss != NULL)
  {
    mainState = STATE_STAGE_FINISHED;
    counter = 80;
    sound.tone(NOTE_G2, 100, NOTE_G3, 150, NOTE_G4, 350);
  }
  else
  {
    mainState = STATE_LEVEL_FINISHED;
    counter = 40;
  }
}

void showStageIntro()
{
  mainState = STATE_STAGE_INTRO;
  counter = 180;
}

void drawMenuOption(uint8_t index, const uint8_t* sprite)
{
  uint8_t halfWidth = pgm_read_byte(sprite) / 2;
  sprites.drawOverwrite(64 - halfWidth, 40 + index * 8, sprite, 0);
  if (index == menuIndex)
  {
    sprites.drawOverwrite(55 - halfWidth, 38 + index * 8, entity_candle, toggle);
    sprites.drawOverwrite(68 + halfWidth, 38 + index * 8, entity_candle, toggle);
  }
}

void loopTitle()
{

// for some reason this takes too much flash so commented it out...
/*
#ifdef DEBUG_CHEAT
  if (ab.pressed(B_BUTTON) && ab.pressed(DOWN_BUTTON))
  {
    Menu::showTitle();
    return;
  }
#endif
*/

  if (ab.everyXFrames(20))
  {
    toggle = !toggle;
  }


  if (state == 0)
  {
    offset = counter * 2;

    if (--counter == 0)
    {
      offset = 1;
      state = 1;
      flashCounter = 6;
      sound.tone(NOTE_GS3, 25, NOTE_G3, 15);
    }
  }
  else
  {
    if (ab.everyXFrames(80))
    {
      offset = -offset;
    }

    if (ab.justPressed(UP_BUTTON) && menuIndex > 0)
    {
      --menuIndex;
      sound.tone(NOTE_E6, 15);
    }

    if (ab.justPressed(DOWN_BUTTON) && menuIndex < TITLE_OPTION_MAX)
    {
      ++menuIndex;
      sound.tone(NOTE_E6, 15);
    }

    if (ab.justPressed(A_BUTTON))
    {
      switch (menuIndex)
      {
        case TITLE_OPTION_PLAY:
          mainState = STATE_STAGE_INTRO;
          counter = 100;
          sound.tone(NOTE_CS6, 30);
          break;
        case TITLE_OPTION_HELP:
          mainState = STATE_HELP;
          sound.tone(NOTE_CS6, 30);
          break;
        case TITLE_OPTION_SFX:
          if (ab.audio.enabled())
          {
            ab.audio.off();
          }
          else
          {
            ab.audio.on();
          }
          ab.audio.saveOnOff();
          sound.tone(NOTE_CS6, 30);
          break;
      }
    }

    drawMenuOption(TITLE_OPTION_PLAY, text_play);
    drawMenuOption(TITLE_OPTION_HELP, text_help);
    drawMenuOption(TITLE_OPTION_SFX, ab.audio.enabled() ? text_sfx_on : text_sfx_off);
  }
  sprites.drawOverwrite(36, 2 - offset, title_left, 0);
  sprites.drawOverwrite(69, 2 + offset, title_right, 0);
}

void loopGameOver()
{
  if (ab.everyXFrames(4))
  {
    if (--counter == 0)
    {
      if (state == 0)
      {
        sound.tone(NOTE_CS6, 30, NOTE_CS7, 40);
        flashCounter = 6;
        state = 1;
      }
      counter = 40;
    }
  }

  uint8_t yOffset = state > 0 ? 0 : counter;

  sprites.drawOverwrite(2, 48 + yOffset / 2, background_mountain, 0);
  sprites.drawOverwrite(0, 44 + yOffset, end_hill, 0);
  sprites.drawOverwrite(20, 36 + yOffset, tileset, 8);
  sprites.drawOverwrite(47, 8 - yOffset, text_game_over, 0);

  if (state == 1)
  {
    sprites.drawOverwrite(54, 26, text_score, 0);
    Util::drawNumber(64 , 34, Game::score, ALIGN_CENTER);

    if (ab.justPressed(A_BUTTON))
    {
      Menu::showTitle();
      sound.tone(NOTE_E6, 15);
    }
  }
}

void loopGameFinished()
{
  if (ab.everyXFrames(8))
  {
    if (--counter == 0)
    {
      if (state == 0)
      {
        sound.tones(beat_game_finished);
        state = 1;
        counter = 30;
      }
      else
      {
        if (state < 7)
        {
          ++state;
          if (state == 7)
          {
            flashCounter = 6;
          }
        }
        counter = 80;
      }
    }
  }

  switch (state)
  {
    case 2:
      //sprites.drawOverwrite(50, -10 + counter, text_the_end, 0);
      sprites.drawOverwrite(36, -32 + counter, title_left, 0);
      sprites.drawOverwrite(69, -32 + counter, title_right, 0);
      break;
    case 3:
      sprites.drawOverwrite(49, -32 + counter, end_zcpp, 0);
      break;
    case 4:
      sprites.drawOverwrite(44, -32 + counter, end_zappedcow, 0);
      break;
    case 5:
      sprites.drawOverwrite(44, -32 + counter, end_increment, 0);
      break;
    case 6:
      sprites.drawOverwrite(50, 8 + counter, text_the_end, 0);
      break;
    case 7:
      sprites.drawOverwrite(50, 8, text_the_end, 0);
      sprites.drawOverwrite(54/* + shift*/, 26, text_score, 0);
      Util::drawNumber(64/* + shift*/, 34, Game::score, ALIGN_CENTER);

      if (ab.justPressed(A_BUTTON))
      {
        Menu::showTitle();
        sound.tone(NOTE_E6, 15);
      }
      break;
  }

  uint8_t playerFrame = (state > 0 && (counter % 20) < 10) ? 1 : 0;
  uint8_t yOffset = state > 0 ? 0 : counter;
  sprites.drawOverwrite(2, 48 + yOffset / 2, background_mountain, 0);
  sprites.drawOverwrite(0, 44 + yOffset, end_hill, 0);
  sprites.drawOverwrite(16, 28 + yOffset, end_player, playerFrame);
}

void Menu::loop()
{
  switch (mainState)
  {
    case STATE_TITLE:
      loopTitle();
      break;
    case STATE_HELP:
      sprites.drawOverwrite(24, 12, help_screen, 0);
      if (ab.justPressed(A_BUTTON))
      {
        Menu::showTitle();
        sound.tone(NOTE_E6, 15);
      }
      break;
    case STATE_PLAY:
      Game::loop();
      break;
    case STATE_STAGE_INTRO:
      if (ab.everyXFrames(16))
      {
        toggle = !toggle;
      }
      sprites.drawOverwrite(52, 18, text_stage, 0);
      Util::drawNumber(75, 18, stage, ALIGN_LEFT);
      sprites.drawPlusMask(56, 32, player_plus_mask, toggle ? 2 : 0);
      if (--counter == 0)
      {
        Game::timeLeft = GAME_STARTING_TIME;
        Game::play();
      }
      break;
    case STATE_GAME_OVER:
      loopGameOver();
      break;
    case STATE_GAME_FINISHED:
      loopGameFinished();
      break;
    case STATE_LEVEL_FINISHED:
      if (--counter == 0)
      {
        Game::play();
      }
      break;
    case STATE_STAGE_FINISHED:
      if (Game::timeLeft > 0)
      {
        if (counter > 0)
        {
          --counter;
        }
        else
        {
          Game::score += SCORE_PER_SECOND;
          if (Game::timeLeft > FPS)
          {
            Game::timeLeft -= FPS;
          }
          else
          {
            Game::timeLeft = 0;
            counter = 60;
          }

          if (ab.everyXFrames(8))
          {
            sound.tone(NOTE_G4, 25);
          }
        }
      }
      else if (Player::hp < PLAYER_MAX_HP)
      {
        if (--counter == 0)
        {
          ++Player::hp;
          sound.tone(NOTE_G3, 60);
          counter = Player::hp == PLAYER_MAX_HP ? 90 : 20;
        }
      }
      else if (--counter == 0)
      {
        if (stage == STAGE_MAX)
        {
          Game::score += Game::life * SCORE_PER_LIFE;

          mainState = STATE_GAME_FINISHED;
          counter = 32;
          state = 0;
        }
        else
        {
          ++stage;
          showStageIntro();
        }
      }

      Game::loop();
      if (Game::timeLeft > 0)
      {
        ab.fillRect(0, 21, 128, 22, BLACK);
        sprites.drawOverwrite(54, 23, text_score, 0);
        Util::drawNumber(64, 35, Game::score, ALIGN_CENTER);
      }
      break;
    case STATE_PLAYER_DIED:
      if (--counter == 0)
      {
        if (Game::life == 0)
        {
          mainState = STATE_GAME_OVER;
          counter = 32;
          state = 0;
        }
        else
        {
          Player::hp = PLAYER_MAX_HP;
          Game::play();
        }
      }
      else if (counter < 100)
      {

        if (Game::timeLeft == 0)
        {
          sprites.drawOverwrite(47, 29, text_time_up, 0);
        }
        else
        {
          sprites.drawOverwrite(57, 29, ui_life_count, 0);
          if (counter > 80)
          {
            Util::drawNumber(69, 29, Game::life + 1, ALIGN_LEFT);
          }
          else if (counter > 70)
          {
            Util::drawNumber(69, 28, Game::life, ALIGN_LEFT);
          }
          else
          {
            Util::drawNumber(69, 29, Game::life, ALIGN_LEFT);
          }

          if (counter == 80)
          {
            sound.tone(NOTE_GS3, 15);
          }
        }
      }
      else
      {
        Game::loop();
      }
      break;
  }
}

