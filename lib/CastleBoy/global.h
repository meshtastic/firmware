#ifndef GLOBAL_H
#define GLOBAL_H

#include <Arduboy2.h>
#include <ArduboyTones.h>

extern Arduboy2Base ab;
extern Sprites sprites;
extern ArduboyTones sound;
extern uint8_t mainState;
extern uint8_t flashCounter;

//#define DEBUG_LOG // enable to show debug logs (LOG_DEBUG)
//#define DEBUG_CPU // enable to display cpu load
//#define DEBUG_RAM // enable to display RAM usage
//#define DEBUG_CHEAT // enable to reset level with A+B+down
//#define DEBUG_HITBOX // enable to show hitboxes

#define FPS 60

#define STAGE_MAX 3
#define LEVEL_PER_STAGE 4
#define ENTITY_MAX 32

// divider for fixed fractional numbers
#define F_PRECISION 1000

// game
#define GAME_STARTING_LIFE 5
#define GAME_STARTING_TIME 15000 // frames (250 secs)
#define GAME_EXTRA_TIME 900 // frames (15 secs)
#define BOSS_MAX_HP 12
#define CAMERA_LEFT_BUFFER 24
#define CAMERA_RIGHT_BUFFER 86
#define SCORE_PER_SECOND 5
#define SCORE_PER_CANDLE 10
#define SCORE_PER_MONSTER 25
#define SCORE_PER_COIN 100
#define SCORE_PER_KNIFE 200
#define SCORE_PER_LIFE 1000

// player
#define PLAYER_JUMP_GRAVITY_F 190 // 0.18
#define PLAYER_FALL_GRAVITY_F 190 // 0.18
#define PLAYER_JUMP_FORCE_F 3100 // 3.0
#define PLAYER_LEVITATE_DURATION 4 // frames
#define PLAYER_KNOCKBACK_DURATION 24 //frames
#define PLAYER_KNOCKBACK_FAST 18 // frames
#define PLAYER_INVINCIBLE_DURATION 120
#define PLAYER_SPEED_NORMAL 2 // every 2 frames
#define PLAYER_SPEED_DUCK 4 // every 4 frames
#define PLAYER_SPEED_KNOCKBACK_NORMAL 2 // every 2 frames
#define PLAYER_SPEED_KNOCKBACK_FAST 1 // every 1 frame
#define PLAYER_ATTACK_TOTAL_DURATION 14 // frames
#define PLAYER_ATTACK_CHARGE 8 // frames
#define PLAYER_MAX_HP 5

// entities
#define ENTITY_FALLING_PLATFORM_DURATION 40
#define ENTITY_FALLING_PLATFORM_WARNING 12
#define BOSS_KNIGHT_WALK_INTERVAL 22
#define BOSS_HARPY_WALK_INTERVAL 16
#define ENTITY_BIRD_WALK_INTERVAL 10

// game state
#define STATE_TITLE 0
#define STATE_STAGE_INTRO 1
#define STATE_PLAY 2
#define STATE_GAME_OVER 3
#define STATE_GAME_FINISHED 4
#define STATE_LEVEL_FINISHED 5
#define STATE_STAGE_FINISHED 6
#define STATE_PLAYER_DIED 7
#define STATE_HELP 8

// map data
#define TILE_DATA_EMPTY 0
#define TILE_DATA_PROP 1
#define TILE_DATA_MISC 2
#define TILE_DATA_MAIN 3

// tile sprite
#define TILE_WALL 0
#define TILE_WALL_ALT 1
#define TILE_SOLID_END 2
#define TILE_SOLID_END_ALT 3
#define TILE_GROUND_START 4
#define TILE_GROUND 5
#define TILE_GROUND_START_ALT 6
#define TILE_GROUND_ALT 7
#define TILE_GRAVE 8
#define TILE_CHAIN 9
#define TILE_WINDOW 10

// entity score
#define PICKUP_COIN_VALUE 180 // frames
#define PICKUP_KNIFE_VALUE 3 // knifes

// size stuffs
#define TILE_WIDTH 8
#define TILE_HEIGHT 8
#define HALF_TILE_WIDTH 4
#define HALF_TILE_HEIGHT 4
#define MAP_WIDTH_MAX 16
#define MAP_HEIGHT_MAX 6

struct Vec
{
  int16_t x;
  int8_t y;
};

struct Box
{
  uint8_t x;
  uint8_t y;
  uint8_t width;
  uint8_t height;
};

#define ALIGN_LEFT 0
#define ALIGN_CENTER 1
#define ALIGN_RIGHT 2

namespace Util
{
  void toggle(uint8_t & flags, uint8_t mask);
  bool collideRect(int16_t x1, int8_t y1, uint8_t width1, uint8_t height1, int16_t x2, int8_t y2, uint8_t width2, uint8_t height2);
  void drawNumber(int16_t x, int16_t y, uint16_t value, uint8_t align);
}

#ifdef DEBUG_LOG
extern int16_t debugValue;
#define LOG_DEBUG(x) debugValue = x
void drawDebugLog();
#else
#define LOG_DEBUG(x)
#endif

#ifdef DEBUG_CPU
void drawDebugCpu();
#endif

#ifdef DEBUG_RAM
void drawDebugRam();
#endif

#endif
