#include "Breakout.h"

// ===========================================================================
// Pure BreakoutGame logic (no display/FS dependencies; always compiled)
// ===========================================================================

uint32_t BreakoutGame::nextRandom()
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng = x;
    return x;
}

void BreakoutGame::buildBricks()
{
    for (uint8_t r = 0; r < BRICK_ROWS; r++)
        for (uint8_t c = 0; c < BRICK_COLS; c++)
            bricks[r][c] = 1;
    bricksLeft = static_cast<uint16_t>(BRICK_ROWS) * BRICK_COLS;
}

void BreakoutGame::serveBall()
{
    // Centre the paddle and launch the ball upward from just above it, at a slight angle whose
    // side is chosen randomly so successive serves are not identical.
    paddleLeft = (BOARD_W - PADDLE_W) / 2;
    ballPxX = static_cast<int32_t>(BOARD_W / 2) * SUBPX;
    ballPxY = static_cast<int32_t>(PADDLE_Y - 2) * SUBPX;
    ballVx = (nextRandom() & 1u) ? 28 : -28;
    ballVy = -BALL_VY;
}

void BreakoutGame::nextLevel()
{
    levelNum++;
    buildBricks();
    serveBall();
}

void BreakoutGame::reset(uint32_t seed)
{
    rng = seed ? seed : 0xA5A5A5A5u; // xorshift32 must never be seeded with 0
    points = 0;
    livesLeft = START_LIVES;
    levelNum = 1;
    alive = true;
    buildBricks();
    serveBall();
}

void BreakoutGame::moveLeft()
{
    if (!alive)
        return;
    paddleLeft -= PADDLE_STEP;
    if (paddleLeft < 0)
        paddleLeft = 0;
}

void BreakoutGame::moveRight()
{
    if (!alive)
        return;
    paddleLeft += PADDLE_STEP;
    if (paddleLeft > BOARD_W - PADDLE_W)
        paddleLeft = BOARD_W - PADDLE_W;
}

bool BreakoutGame::step()
{
    if (!alive)
        return false;

    ballPxX += ballVx;
    ballPxY += ballVy;

    int16_t px = static_cast<int16_t>(ballPxX / SUBPX);
    int16_t py = static_cast<int16_t>(ballPxY / SUBPX);

    // Side walls.
    if (px <= 0) {
        ballPxX = 0;
        px = 0;
        ballVx = -ballVx;
    } else if (px >= BOARD_W - 1) {
        ballPxX = static_cast<int32_t>(BOARD_W - 1) * SUBPX;
        px = BOARD_W - 1;
        ballVx = -ballVx;
    }
    // Top wall.
    if (py <= 0) {
        ballPxY = 0;
        py = 0;
        ballVy = -ballVy;
    }

    // Bricks: at most one brick per step (single, blocky reflection off the bottom/top face).
    if (py >= BRICK_TOP && py < BRICK_TOP + BRICK_ROWS * BRICK_H) {
        const int r = (py - BRICK_TOP) / BRICK_H;
        const int c = px / BRICK_W;
        if (r >= 0 && r < BRICK_ROWS && c >= 0 && c < BRICK_COLS && bricks[r][c]) {
            bricks[r][c] = 0;
            bricksLeft--;
            points += POINTS_PER_BRICK;
            ballVy = -ballVy;
            if (bricksLeft == 0) {
                nextLevel();
                return true;
            }
        }
    }

    // Paddle: bounce up and steer horizontally based on where the ball struck.
    if (ballVy > 0 && py >= PADDLE_Y - 1 && py <= PADDLE_Y + PADDLE_H) {
        if (px >= paddleLeft && px < paddleLeft + PADDLE_W) {
            ballPxY = static_cast<int32_t>(PADDLE_Y - 1) * SUBPX;
            ballVy = -BALL_VY;
            // Six zones across the paddle map to increasing outward angles; no zone is vertical.
            static const int16_t vxByZone[6] = {-48, -28, -8, 8, 28, 48};
            int zone = ((px - paddleLeft) * 6) / PADDLE_W;
            if (zone < 0)
                zone = 0;
            else if (zone > 5)
                zone = 5;
            ballVx = vxByZone[zone];
        }
    }

    // Ball lost past the bottom edge.
    if (py >= BOARD_H) {
        if (livesLeft > 0)
            livesLeft--;
        if (livesLeft == 0) {
            alive = false;
            return false;
        }
        serveBall();
    }

    return alive;
}

// ===========================================================================
// Breakout adapter (display + persistence; BaseUI games build only)
// ===========================================================================

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/images.h"
#include "main.h"

Breakout::Breakout()
{
    scores_.load();
}

int32_t Breakout::tickIntervalMs() const
{
    // Speed ramps with level: 45 ms base, 5 ms per level, floor 20 ms.
    int32_t iv = 45 - static_cast<int32_t>(game.level() - 1) * 5;
    return iv < 20 ? 20 : iv;
}

void Breakout::handleInput(input_broker_event ev)
{
    switch (ev) {
    case INPUT_BROKER_LEFT:
        game.moveLeft();
        break;
    case INPUT_BROKER_RIGHT:
        game.moveRight();
        break;
    default:
        break;
    }
}

void Breakout::drawAttract(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setColor(WHITE);
    const int16_t w = display->getWidth();
    const int16_t cx = x + w / 2;
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(cx, y, "B R E A K O U T");
    display->drawXbm(x + (w - breakout_width) / 2, y + 15, breakout_width, breakout_height, breakout);
    char hi[32];
    if (scores_.scoreAt(0) > 0 && scores_.nameAt(0)[0] != '\0')
        snprintf(hi, sizeof(hi), "High: %s %lu", scores_.nameAt(0), static_cast<unsigned long>(scores_.scoreAt(0)));
    else
        snprintf(hi, sizeof(hi), "High: %lu", static_cast<unsigned long>(scores_.scoreAt(0)));
    display->drawString(cx, y + 34, hi);
    display->drawString(cx, y + 48, "SELECT to play");
}

void Breakout::drawPlaying(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setColor(WHITE);
    display->setFont(FONT_SMALL);

    // Score row (top-left), remaining lives as small squares (top-right).
    char buf[16];
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "Sc %lu", static_cast<unsigned long>(game.score()));
    display->drawString(x + 2, y, buf);
    for (uint8_t i = 0; i < game.lives(); i++)
        display->fillRect(x + display->getWidth() - 3 - i * 4, y + 2, 2, 2);

    // Bricks.
    for (uint8_t r = 0; r < BreakoutGame::BRICK_ROWS; r++)
        for (uint8_t c = 0; c < BreakoutGame::BRICK_COLS; c++)
            if (game.brickAt(r, c))
                display->fillRect(x + c * BreakoutGame::BRICK_W, y + BreakoutGame::BRICK_TOP + r * BreakoutGame::BRICK_H,
                                  BreakoutGame::BRICK_W - 1, BreakoutGame::BRICK_H - 1);

    // Paddle.
    display->fillRect(x + game.paddleX(), y + BreakoutGame::PADDLE_Y, BreakoutGame::PADDLE_W, BreakoutGame::PADDLE_H);

    // Ball.
    display->fillRect(x + game.ballX(), y + game.ballY(), 2, 2);
}

#endif // HAS_SCREEN && BASEUI_HAS_GAMES
