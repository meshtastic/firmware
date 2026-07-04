#include "player.h"

#include "game.h"
#include "map.h"
#include "entity.h"
#include "assets.h"

#define SPRITE_ORIGIN_X 8
#define SPRITE_ORIGIN_Y 16

#define FRAME_IDLE 0
#define FRAME_WALK_1 0
#define FRAME_WALK_2 2
#define FRAME_ATTACK_CHARGE 4
#define FRAME_ATTACK 6
#define FRAME_AIR 8
#define FRAME_KNOCKBACK 9
#define FRAME_DEAD 10

#define FRAME_FLIPPED_OFFSET 11

#define WALK_FRAME_RATE 12

uint8_t Player::hp;
Vec Player::pos;
bool Player::alive;
uint8_t Player::knifeCount;

namespace
{
const Box normalHitbox =
{
  4, 14, // x, y
  8, 14  // width, height
};

const Box duckHitbox =
{
  4, 6, // x, y
  8, 6  // width, height
};

const Box knifeHitbox =
{
  0, 1, // x, y
  8, 4 // width, height
};

int8_t velocityX;
int16_t velocityYf;
// FIXME move flags to a single byte and use masking (is it worth it?)
bool grounded;
uint8_t attackCounter;
bool knifeAttack;
bool jumping;
bool ducking;
uint8_t knockbackCounter;
uint8_t levitateCounter;
uint8_t invincibleCounter;
bool flipped;
bool walkFrame;

bool knife;
bool knifeFlipped;
Vec knifePosition;

bool visible;

} // unamed

void Player::init(int16_t x, int8_t y)
{
  pos.x = x;
  pos.y = y;
  grounded = true;
  attackCounter = 0;
  knifeAttack = false;
  alive = true;
  jumping = false;
  ducking = false;
  visible = true;
  flipped = false;
  knockbackCounter = 0;
  invincibleCounter = 0;
  levitateCounter = 0;
  velocityX = 0;
  velocityYf = 0;
  knife = false;
}

void Player::update()
{  
  // knockback
  if (knockbackCounter > 0)
  {
    if (--knockbackCounter == 0)
    {
      velocityX = 0;
      if (hp == 0)
      {
        alive = false;
      }
    }
  }

  if (invincibleCounter > 0)
  {
    if (--invincibleCounter == 0)
    {
      visible = true;
    }
  }

  // not alive
  if (!alive)
  {
    return;
  }

  // attack
  if (knockbackCounter == 0 && attackCounter == 0)
  {
    if (ab.justPressed(B_BUTTON))
    {
      knifeAttack = false;
      attackCounter = PLAYER_ATTACK_TOTAL_DURATION;
      sound.tone(NOTE_GS4, 10);
    }
    else if (ab.justPressed(UP_BUTTON))
    {
      if (knifeCount > 0)
      {
        --knifeCount;
        knifeAttack = true;
        attackCounter = PLAYER_ATTACK_TOTAL_DURATION;
        sound.tone(NOTE_GS5, 10);
      }
      else
      {
        sound.tone(NOTE_G2, 5);
      }
    }
  }

  // jump
  if (knockbackCounter == 0 && !ducking && attackCounter == 0 && grounded && ab.justPressed(A_BUTTON))
  {
    // start jumping
    grounded = false;
    jumping = true;
    velocityYf = -PLAYER_JUMP_FORCE_F;
  }

  // duck
  if (knockbackCounter == 0 && attackCounter == 0)
  {
    if (!ducking)
    {
      ducking = grounded && ab.pressed(DOWN_BUTTON);
    }
    else if (!ab.pressed(DOWN_BUTTON))
    {
      // only stop ducking if player can stand
      ducking = Map::collide(pos.x, pos.y, normalHitbox);
    }
  }

  const Box& hitbox = ducking ? duckHitbox : normalHitbox;

  // vertical movement: levitation (middle of jump)
  if (levitateCounter > 0)
  {
    --levitateCounter;
  }
  // vertical movement: jump
  else if (jumping)
  {
    velocityYf += PLAYER_JUMP_GRAVITY_F;
    if (velocityYf >= 0)
    {
      velocityYf = 0;
      jumping = false;
      levitateCounter = PLAYER_LEVITATE_DURATION;
    }
    else
    {
      Game::moveY(pos, velocityYf / F_PRECISION, ducking ? duckHitbox : normalHitbox, true);
    }
  }
  // vertical movement: walk
  else
  {
    velocityYf += PLAYER_FALL_GRAVITY_F;
    int16_t offsetY = velocityYf / F_PRECISION;
    if (offsetY > 0)
    {
      grounded = Game::moveY(pos, offsetY, hitbox, true);
    }
    else
    {
      grounded = Entities::moveCollide(pos.x, pos.y, 0, 1, hitbox) || Map::collide(pos.x, pos.y + 1, hitbox);
    }

    if (grounded)
    {
      velocityYf = 0;
    }
  }

  // horizontal movement: input
  if (knockbackCounter == 0)
  {
    if (attackCounter == 0 && ab.pressed(LEFT_BUTTON))
    {
      velocityX = -1;
      flipped = true;
    }
    else if (attackCounter == 0 && ab.pressed(RIGHT_BUTTON))
    {
      velocityX = 1;
      flipped = false;
    }
    else if (grounded)
    {
      velocityX = 0;
    }
  }

  // horizontal movement: physic
  if (velocityX != 0 &&
      (
        // normal speed
        knockbackCounter == 0 && ab.everyXFrames(ducking ? PLAYER_SPEED_DUCK : PLAYER_SPEED_NORMAL) ||
        // knockback speed
        knockbackCounter > 0 && ab.everyXFrames(knockbackCounter < PLAYER_KNOCKBACK_FAST ? PLAYER_SPEED_KNOCKBACK_NORMAL : PLAYER_SPEED_KNOCKBACK_FAST)
      )
     )
  {
    if (!Entities::moveCollide(pos.x, pos.y, velocityX, 0, hitbox) && !Map::collide(pos.x + velocityX, pos.y, hitbox))
    {
      pos.x += velocityX;
    }
  }

  // perform attack
  if (attackCounter > 0)
  {
    if (--attackCounter <= PLAYER_ATTACK_CHARGE)
    {
      if (knifeAttack)
      {
        if (attackCounter == PLAYER_ATTACK_CHARGE)
        {
          knife = true;
          knifePosition.x = pos.x + (flipped ? -14 : 6);
          knifePosition.y = pos.y - (ducking ? 6 : 14);
          knifeFlipped = flipped;
        }
      }
      else
      {
        Entities::damage(pos.x + (flipped ? -24 : 0), pos.y - (ducking ? 4 : 11), 24, 2, 2);
#ifdef DEBUG_HITBOX
        ab.fillRect(pos.x + (flipped ? -24 : 0) - Game::cameraX, pos.y - (ducking ? 4 : 12), 24, 2, WHITE);
#endif
      }
    }
  }

  // check if player falled in a hole
  if (pos.y - SPRITE_ORIGIN_Y > 64)
  {
    alive = false;
  }

  // knife
  if (knife)
  {
    knifePosition.x += knifeFlipped ? -3 : 3;
    if (Entities::damage(knifePosition.x - knifeHitbox.x, knifePosition.y - knifeHitbox.y, knifeHitbox.width, knifeHitbox.height, 1))
    {
      knife = false;
    }

    if (Map::collide(knifePosition.x, knifePosition.y, knifeHitbox))
    {
      knife = false;
    }

    if (knifePosition.x + knifeHitbox.width < Game::cameraX || knifePosition.x > Game::cameraX + 128)
    {
      knife = false;
    }
  }

  // check entity collision
  Entity* entity = Entities::checkPlayer(pos.x - hitbox.x, pos.y - hitbox.y, hitbox.width, hitbox.height);
  if (hp > 0 && invincibleCounter == 0 && entity != NULL)
  {
    flipped = entity->pos.x < pos.x;
    velocityX = flipped ? 1 : -1;
    knockbackCounter = PLAYER_KNOCKBACK_DURATION;
    jumping = false;
    levitateCounter = 0;
    attackCounter = 0;
    if (--hp > 0)
    {
      invincibleCounter = PLAYER_INVINCIBLE_DURATION;
    }
    flashCounter = 2;
    sound.tone(NOTE_GS3, 25, NOTE_G3, 15);
  }
}

void Player::draw()
{
  uint8_t frame = 0;

  if (!alive)
  {
    frame = FRAME_DEAD;
  }
  else if (knockbackCounter > 0)
  {
    frame = FRAME_KNOCKBACK;
  }
  else if (attackCounter == 0 && !grounded)
  {
    frame = FRAME_AIR;
  }
  else
  {
    if (attackCounter == 0)
    {
      if (velocityX == 0)
      {
        frame = FRAME_IDLE;
      }
      else
      {
        if (ab.everyXFrames(WALK_FRAME_RATE))
        {
          walkFrame = !walkFrame;
        }
        frame = walkFrame ? FRAME_WALK_2 : FRAME_WALK_1;
      }
    }
    else if (knifeAttack)
    {
      frame = FRAME_ATTACK;
    }
    else if (attackCounter < PLAYER_ATTACK_CHARGE)
    {
      frame = FRAME_ATTACK;
    }
    else
    {
      frame = FRAME_ATTACK_CHARGE;
    }

    if (ducking)
    {
      frame++;
    }
  }

  if (ab.everyXFrames(4) && knockbackCounter == 0 && invincibleCounter > 0)
  {
    visible = !visible;
  }

  if (visible)
  {
    sprites.drawPlusMask(pos.x - SPRITE_ORIGIN_X - Game::cameraX, pos.y - SPRITE_ORIGIN_Y, player_plus_mask, frame + (flipped ? FRAME_FLIPPED_OFFSET : 0));

    if (attackCounter != 0 && !knifeAttack && attackCounter < PLAYER_ATTACK_CHARGE)
    {
      sprites.drawPlusMask(pos.x + (flipped ? -24 : 8) - Game::cameraX , pos.y - (ducking ? 4 : 12), flipped ? player_attack_left_plus_mask : player_attack_right_plus_mask, 0);
    }
  }

  if (knife)
  {
    sprites.drawPlusMask(knifePosition.x - Game::cameraX, knifePosition.y, entity_knife_plus_mask, flipped);
  }

#ifdef DEBUG_HITBOX
  Box hitbox = ducking ? duckHitbox : normalHitbox;
  ab.fillRect(pos.x - hitbox.x - Game::cameraX, pos.y - hitbox.y, hitbox.width, hitbox.height, WHITE);
#endif

}

