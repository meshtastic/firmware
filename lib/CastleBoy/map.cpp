#include "map.h"

#include "game.h"
#include "player.h"
#include "entity.h"
#include "assets.h"

uint8_t Map::width;
bool Map::showBackground;
Entity* Map::boss = NULL;

namespace
{
const uint8_t* tilemap;
uint8_t height;

uint8_t solidTileIndex;
uint8_t mainTile;
uint8_t mainTileAlt;
uint8_t mainStartTile;
uint8_t mainStartTileAlt;
bool endMainTile;
uint8_t propTile;
uint8_t miscTile;

uint8_t getTileAt(uint8_t x, uint8_t y)
{
  return (pgm_read_byte(tilemap + (x * height + y) / 4) & (0x03 << (y % 4) * 2)) >> (y % 4) * 2;
}

}  // unamed

void Map::init(const uint8_t* source)
{
  // map size
  width = pgm_read_byte(source);
  height = pgm_read_byte(++source);

  uint8_t temp = pgm_read_byte(++source);
  bool hasBoss = (temp & 0xC0) == 0xC0;

  // rendering style (indoor, outdoor, garden)
  if (temp & 0x80)
  {
    // indoor
    solidTileIndex = 3;

    mainTile = TILE_WALL;
    mainTileAlt = TILE_WALL_ALT;
    mainStartTile = TILE_WALL;
    mainStartTileAlt = TILE_WALL_ALT;
    endMainTile = false;

    miscTile = TILE_CHAIN;

    propTile = TILE_WINDOW;
    showBackground = false;
  }
  else
  {
    // outdoor/cave
    solidTileIndex = 2;

    mainTile = TILE_GROUND;
    mainTileAlt = TILE_GROUND_ALT;
    
    miscTile = TILE_WALL;

    if (temp & 0x40)
    {
      // outdoor
      mainStartTile = TILE_GROUND_START;
      mainStartTileAlt = TILE_GROUND_START_ALT;
      //endMainTile = false;
      showBackground = true;
      propTile = TILE_GRAVE;
    }
    else
    {
      // cave
      mainStartTile = TILE_GROUND;
      mainStartTileAlt = TILE_GROUND_ALT;
      //endMainTile = true;
      showBackground = false;
      propTile = TILE_GRAVE;
    }

    endMainTile = true;
  }

  // player starting position
  uint8_t playerY = temp & 0x0F;
  uint8_t playerX = pgm_read_byte(++source);
  Player::init(playerX * TILE_WIDTH + HALF_TILE_WIDTH, playerY * TILE_HEIGHT + TILE_HEIGHT);

  // tile map position
  tilemap = ++source;

  // entities
  source += width * height / 4;
  uint8_t entityCount = pgm_read_byte(source);
  boss = NULL;
  for (uint8_t i = 0; i < entityCount; i++)
  {
    temp = pgm_read_byte(++source);
    uint8_t entityType = (temp & 0xF0) >> 4;
    uint8_t x = pgm_read_byte(++source);
    uint8_t y = temp & 0x0F;

    Entity* entity = Entities::add(entityType, x * TILE_WIDTH + HALF_TILE_WIDTH, y * TILE_HEIGHT + TILE_HEIGHT);
    if (hasBoss)
    {
      boss = entity;
    }
  }
}

// FIXME can we use uint8_t instead of int16_t ?
bool Map::collide(int16_t x, int8_t y, const Box& hitbox)
{
  x -= hitbox.x;
  y -= hitbox.y;

  if (x < 0 /*|| x + hitbox.width > width * TILE_WIDTH*/)
  {
    // cannot get out on the sides, collide
    // WARNING can get out of right side, if we use this for projectile
    // we might need to change this..
    return true;
  }

  int16_t tx1 = x / TILE_WIDTH;
  int8_t ty1 = y / TILE_HEIGHT;
  int16_t tx2 = (x + hitbox.width - 1) / TILE_WIDTH;
  int8_t ty2 = (y + hitbox.height - 1) / TILE_HEIGHT;

  if (ty2 < 0 || ty2 >= height)
  {
    // either higher or lower than map, no collision
    return false;
  }

  // clamp positions
  if (tx1 < 0) tx1 = 0;
  if (tx2 >= width) tx2 = width - 1;
  if (ty1 < 0) ty1 = 0;
  if (ty2 >= height) ty2 = height - 1;

  // perform hit test on selected tiles
  for (int16_t ix = tx1; ix <= tx2; ix++)
  {
    for (int8_t iy = ty1; iy <= ty2; iy++)
    {
      if (getTileAt(ix, iy) >= solidTileIndex)
      {
        // check for rectangle intersection
        if (Util::collideRect(ix * TILE_WIDTH, iy * TILE_HEIGHT, TILE_WIDTH, TILE_HEIGHT, x, y, hitbox.width, hitbox.height))
        {
          return true;
        }
      }
    }
  }

  return false;
}

// can be optimized CPU wise if needed (by not using getTileAt)
void Map::draw()
{
  uint8_t start = Game::cameraX / 8;

  for (uint8_t ix = start; ix < start + 17; ix++)
  {
    bool isMain = false;
    bool needToEndTile = false;
    bool tileEnded = false;
    for (uint8_t iy = 0; iy < height; iy++)
    {
      uint8_t tile = getTileAt(ix, iy);
      if (tile == TILE_DATA_EMPTY)
      {
        if (!tileEnded && needToEndTile)
        {
          tile = ix % 2 == 0 ? TILE_SOLID_END : TILE_SOLID_END_ALT;
          needToEndTile = false;
          tileEnded = true;
        }
        else
        {
          continue;
        }
      }
      else if (tile == TILE_DATA_MISC)
      {
        tile = miscTile;
        isMain = false;
      }
      else if (tile == TILE_DATA_MAIN)
      {
        bool useAlt = ix % 2 == 0 && iy % 2 == 1 || ix % 2 == 1 && iy & 2 == 0;
        if (isMain)
        {
          // already started, use normal main tile
          tile = useAlt ? mainTileAlt : mainTile;
        }
        else
        {
          // first main tile from top
          tile = useAlt ? mainStartTileAlt : mainStartTile;
          isMain = true;
        }
        if(endMainTile)
        {
          needToEndTile = true;
        }
      }
      else // tile == TILE_DATA_PROP
      {
        tile = propTile;
        needToEndTile = false;
      }

      sprites.drawOverwrite(ix * TILE_WIDTH - Game::cameraX, iy * TILE_HEIGHT, tileset, tile);
    }
  }
}

