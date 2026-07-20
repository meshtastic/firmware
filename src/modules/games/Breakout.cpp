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
    ballTick = false;
    buildBricks();
    serveBall();
}

void BreakoutGame::movePaddle(int16_t dxPx)
{
    if (!alive)
        return;
    paddleLeft += dxPx;
    if (paddleLeft < 0)
        paddleLeft = 0;
    else if (paddleLeft > BOARD_W - PADDLE_W)
        paddleLeft = BOARD_W - PADDLE_W;
}

void BreakoutGame::moveLeft()
{
    movePaddle(-PADDLE_STEP);
}

void BreakoutGame::moveRight()
{
    movePaddle(PADDLE_STEP);
}

bool BreakoutGame::step()
{
    if (!alive)
        return false;

    // The ball advances on every other step() so the caller can tick (and poll the paddle) at twice
    // the ball's rate -- this keeps the ball speed constant while paddle control refreshes faster.
    ballTick = !ballTick;
    if (!ballTick)
        return true;

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
#include "graphics/TFTColorRegions.h"
#include "graphics/TFTPalette.h"
#include "graphics/images.h"
#include "main.h"
#if ARCH_PORTDUINO && defined(__linux__)
#include "input/LinuxJoystick.h"
#endif

// Paddle pixels moved per tick while a joystick direction is held (continuous polling path).
static constexpr int16_t PADDLE_POLL_STEP = 3;

#if GRAPHICS_TFT_COLORING_ENABLED
// Classic Breakout brick-wall colours, top row to bottom.
static uint16_t brickRowColor(uint8_t row)
{
    using namespace graphics;
    switch (row) {
    case 0:
        return TFTPalette::Red;
    case 1:
        return TFTPalette::Orange;
    case 2:
        return TFTPalette::Yellow;
    case 3:
        return TFTPalette::Green;
    default:
        return TFTPalette::Cyan;
    }
}
#endif

Breakout::Breakout()
{
    scores_.load();
}

int32_t Breakout::tickIntervalMs() const
{
    // Tick at twice the ball's cadence: the ball advances every other step() (BreakoutGame::step),
    // so halving the interval keeps the ball speed the same while the paddle is polled/redrawn twice
    // as often. Speed ramps with level: ~22 ms base, floor 10 ms.
    int32_t iv = 45 - static_cast<int32_t>(game.level() - 1) * 5;
    if (iv < 20)
        iv = 20;
    return iv / 2;
}

bool Breakout::tick()
{
#if ARCH_PORTDUINO && defined(__linux__)
    // Poll the joystick's held direction directly so the paddle glides continuously instead of
    // creeping along at the D-pad's slow auto-repeat rate.
    if (aLinuxJoystick) {
        const int held = aLinuxJoystick->heldXZone();
        if (held < 0)
            game.movePaddle(-PADDLE_POLL_STEP);
        else if (held > 0)
            game.movePaddle(PADDLE_POLL_STEP);
    }
#endif
    return game.step();
}

void Breakout::handleInput(input_broker_event ev)
{
#if ARCH_PORTDUINO && defined(__linux__)
    // When a joystick is present the paddle is polled continuously in tick(); ignore the discrete
    // (and slow) repeat events so we don't double-move.
    if (aLinuxJoystick)
        return;
#endif
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
    const int16_t logoX = x + (w - breakout_width) / 2;
    const int16_t logoY = y + 15;
    display->drawXbm(logoX, logoY, breakout_width, breakout_height, breakout);
#if GRAPHICS_TFT_COLORING_ENABLED
    // The glyph is three brick courses, a ball, and a paddle -- colour each to match the game.
    const uint16_t abg = graphics::getThemeBodyBg();
    graphics::registerTFTColorRegionDirect(logoX, logoY + 1, breakout_width, 2, graphics::TFTPalette::Red, abg);
    graphics::registerTFTColorRegionDirect(logoX, logoY + 4, breakout_width, 2, graphics::TFTPalette::Yellow, abg);
    graphics::registerTFTColorRegionDirect(logoX, logoY + 7, breakout_width, 2, graphics::TFTPalette::Green, abg);
    graphics::registerTFTColorRegionDirect(logoX + 4, logoY + 14, 8, 2, graphics::TFTPalette::Blue, abg);  // paddle
    graphics::registerTFTColorRegionDirect(logoX + 7, logoY + 10, 2, 2, graphics::TFTPalette::White, abg); // ball
#endif
    char hi[32];
    if (scores_.scoreAt(0) > 0 && scores_.nameAt(0)[0] != '\0')
        snprintf(hi, sizeof(hi), "High: %s %lu", scores_.nameAt(0), static_cast<unsigned long>(scores_.scoreAt(0)));
    else
        snprintf(hi, sizeof(hi), "High: %lu", static_cast<unsigned long>(scores_.scoreAt(0)));
    display->drawString(cx, y + 34, hi);
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

#if GRAPHICS_TFT_COLORING_ENABLED
    // Colour the wall by row, plus a blue paddle and white ball. One region per brick row (the row's
    // lit bricks take the colour; cleared cells and gaps stay background). Paddle then ball register
    // last so the ball always wins where it overlaps.
    const uint16_t bg = graphics::getThemeBodyBg();
    for (uint8_t r = 0; r < BreakoutGame::BRICK_ROWS; r++)
        graphics::registerTFTColorRegionDirect(x, y + BreakoutGame::BRICK_TOP + r * BreakoutGame::BRICK_H, BreakoutGame::BOARD_W,
                                               BreakoutGame::BRICK_H - 1, brickRowColor(r), bg);
    graphics::registerTFTColorRegionDirect(x + game.paddleX(), y + BreakoutGame::PADDLE_Y, BreakoutGame::PADDLE_W,
                                           BreakoutGame::PADDLE_H, graphics::TFTPalette::Blue, bg);
    graphics::registerTFTColorRegionDirect(x + game.ballX(), y + game.ballY(), 2, 2, graphics::TFTPalette::White, bg);
#endif
}

#endif // HAS_SCREEN && BASEUI_HAS_GAMES
