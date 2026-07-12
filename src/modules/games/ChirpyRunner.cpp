#include "ChirpyRunner.h"

// ===========================================================================
// Pure ChirpyRunnerGame logic (no display/FS dependencies; always compiled)
// ===========================================================================

uint32_t ChirpyRunnerGame::nextRandom()
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng = x;
    return x;
}

int16_t ChirpyRunnerGame::pickGapSteps()
{
    return static_cast<int16_t>(GAP_STEPS_MIN + static_cast<int16_t>(nextRandom() % (GAP_STEPS_MAX - GAP_STEPS_MIN + 1)));
}

void ChirpyRunnerGame::spawnObstacle()
{
    for (uint8_t i = 0; i < MAX_OBSTACLES; i++) {
        if (obst[i].active)
            continue;
        obst[i].active = true;
        obst[i].scored = false;
        obst[i].xSub = static_cast<int32_t>(BOARD_W) * SUBPX;
        obst[i].w = OBST_W;
        // Three height tiers so timing varies (kept clearable with margin for a forgiving jump).
        const uint32_t tier = nextRandom() % 3u;
        obst[i].h = tier == 0 ? 8 : (tier == 1 ? 11 : 15);
        return;
    }
}

void ChirpyRunnerGame::reset(uint32_t seed)
{
    rng = seed ? seed : 0xA5A5A5A5u; // xorshift32 must never be seeded with 0
    points = 0;
    alive = true;
    chirpyTop = groundedTopSub();
    vy = 0;
    grounded = true;
    for (uint8_t i = 0; i < MAX_OBSTACLES; i++)
        obst[i] = {};
    speedSub = SPEED_BASE;
    spawnTimer = 0; // first obstacle spawns on the first step
}

void ChirpyRunnerGame::jump()
{
    if (!alive || !grounded)
        return;
    vy = -JUMP_V;
    grounded = false;
}

bool ChirpyRunnerGame::step()
{
    if (!alive)
        return false;

    // --- Chirpy vertical motion ---
    vy += GRAVITY;
    chirpyTop += vy;
    const int32_t gt = groundedTopSub();
    if (chirpyTop >= gt) {
        chirpyTop = gt;
        vy = 0;
        grounded = true;
    } else {
        grounded = false;
    }

    // --- Scroll obstacles, score, retire off-screen ones ---
    for (uint8_t i = 0; i < MAX_OBSTACLES; i++) {
        if (!obst[i].active)
            continue;
        obst[i].xSub -= speedSub;
        const int16_t ox = obstacleX(i);
        if (!obst[i].scored && ox + obst[i].w < CHIRPY_X) {
            obst[i].scored = true;
            points++;
        }
        if (ox + obst[i].w < 0)
            obst[i].active = false;
    }

    // --- Spawn on a tick timer (time-based spacing that scales with speed) ---
    if (spawnTimer > 0)
        spawnTimer--;
    if (spawnTimer <= 0) {
        spawnObstacle();
        spawnTimer = pickGapSteps();
    }

    // --- Difficulty ramp (scroll speed grows with score, then caps) ---
    const uint32_t capped = points < SPEED_CAP_PTS ? points : SPEED_CAP_PTS;
    speedSub = SPEED_BASE + static_cast<int32_t>(capped) * SPEED_INC;

    // --- Collision (forgiving hitbox: skip the antenna, inset the sides) ---
    const int16_t hx = CHIRPY_X + 2;
    const int16_t hxr = hx + (CHIRPY_W - 4);
    const int16_t hBottom = chirpyY() + CHIRPY_H;
    const int16_t hTop = chirpyY() + 4;
    for (uint8_t i = 0; i < MAX_OBSTACLES; i++) {
        if (!obst[i].active)
            continue;
        const int16_t ox = obstacleX(i);
        const int16_t oxr = ox + obst[i].w;
        const int16_t oTop = GROUND_Y - obst[i].h;
        if (hx < oxr && hxr > ox && hTop < GROUND_Y && hBottom > oTop) {
            alive = false;
            return false;
        }
    }

    return alive;
}

// ===========================================================================
// ChirpyRunner adapter (display + persistence; BaseUI games build only)
// ===========================================================================

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/TFTColorRegions.h"
#include "graphics/TFTPalette.h"
#include "graphics/images.h"
#include "main.h"

ChirpyRunner::ChirpyRunner()
{
    scores_.load();
}

void ChirpyRunner::handleInput(input_broker_event ev)
{
    // SELECT is the jump (as requested); UP is accepted as a convenient alternate.
    if (ev == INPUT_BROKER_SELECT || ev == INPUT_BROKER_SELECT_LONG || ev == INPUT_BROKER_UP)
        game.jump();
}

void ChirpyRunner::drawAttract(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setColor(WHITE);
    const int16_t w = display->getWidth();
    const int16_t cx = x + w / 2;
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(cx, y, "CHIRPY RUNNER");
    const int16_t logoX = x + (w - chirpy_run_width) / 2;
    const int16_t logoY = y + 15;
    display->drawXbm(logoX, logoY, chirpy_run_width, chirpy_run_height, chirpy_run);
#if GRAPHICS_TFT_COLORING_ENABLED
    // Chirpy is green, with white eyes. The eyes are the lit pixels at rows 5-7, cols 4-7 of the
    // glyph; a white region registered after the green one wins there.
    graphics::registerTFTColorRegionDirect(logoX, logoY, chirpy_run_width, chirpy_run_height,
                                           graphics::TFTPalette::MeshtasticGreen, graphics::getThemeBodyBg());
    graphics::registerTFTColorRegionDirect(logoX + 4, logoY + 5, 4, 3, graphics::TFTPalette::White, graphics::getThemeBodyBg());
#endif
    char hi[32];
    if (scores_.scoreAt(0) > 0 && scores_.nameAt(0)[0] != '\0')
        snprintf(hi, sizeof(hi), "High: %s %lu", scores_.nameAt(0), static_cast<unsigned long>(scores_.scoreAt(0)));
    else
        snprintf(hi, sizeof(hi), "High: %lu", static_cast<unsigned long>(scores_.scoreAt(0)));
    display->drawString(cx, y + 34, hi);
}

void ChirpyRunner::drawPlaying(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setColor(WHITE);
    display->setFont(FONT_SMALL);

    // Score (top-left).
    char buf[16];
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "Sc %lu", static_cast<unsigned long>(game.score()));
    display->drawString(x + 2, y, buf);

    // Ground line.
    display->drawLine(x, y + ChirpyRunnerGame::GROUND_Y, x + display->getWidth() - 1, y + ChirpyRunnerGame::GROUND_Y);

    // Obstacles (ground-based pillars).
    for (uint8_t i = 0; i < ChirpyRunnerGame::obstacleSlots(); i++) {
        if (!game.obstacleActive(i))
            continue;
        const int16_t oh = game.obstacleH(i);
        display->fillRect(x + game.obstacleX(i), y + ChirpyRunnerGame::GROUND_Y - oh, game.obstacleW(i), oh);
    }

    // Chirpy.
    const int16_t cxp = x + ChirpyRunnerGame::CHIRPY_X;
    const int16_t cyp = y + game.chirpyY();
    display->drawXbm(cxp, cyp, chirpy_run_width, chirpy_run_height, chirpy_run);
#if GRAPHICS_TFT_COLORING_ENABLED
    graphics::registerTFTColorRegionDirect(cxp, cyp, chirpy_run_width, chirpy_run_height, graphics::TFTPalette::MeshtasticGreen,
                                           graphics::getThemeBodyBg());
    graphics::registerTFTColorRegionDirect(cxp + 4, cyp + 5, 4, 3, graphics::TFTPalette::White, graphics::getThemeBodyBg());
#endif
}

#endif // HAS_SCREEN && BASEUI_HAS_GAMES
