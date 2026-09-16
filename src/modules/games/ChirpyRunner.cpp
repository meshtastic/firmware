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

void ChirpyRunnerGame::resetClouds()
{
    // Spread the clouds across the sky at staggered x, at varied heights near the top.
    for (uint8_t i = 0; i < CLOUD_COUNT; i++) {
        cloud[i].xSub = static_cast<int32_t>(i * (BOARD_W / CLOUD_COUNT) + 6) * SUBPX;
        cloud[i].y = static_cast<int16_t>(10 + nextRandom() % 10u); // 10..19 (below the score row)
    }
}

void ChirpyRunnerGame::scrollClouds()
{
    // Slow parallax drift; wrap back to the right (at a fresh height) once off the left edge.
    for (uint8_t i = 0; i < CLOUD_COUNT; i++) {
        cloud[i].xSub -= CLOUD_SPEED_SUB;
        if (cloud[i].xSub / SUBPX + CLOUD_W < 0) {
            cloud[i].xSub = static_cast<int32_t>(BOARD_W + nextRandom() % 24u) * SUBPX;
            cloud[i].y = static_cast<int16_t>(10 + nextRandom() % 10u);
        }
    }
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
        obst[i].h = BUILDING_HEIGHTS[tier];
        obst[i].colorIdx = static_cast<uint8_t>((spawnCount / 10u) % OBST_COLOR_COUNT);
        spawnCount++;
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
    jumpGravity = GRAVITY;
    grounded = true;
    for (uint8_t i = 0; i < MAX_OBSTACLES; i++)
        obst[i] = {};
    speedSub = SPEED_BASE;
    spawnTimer = 0; // first obstacle spawns on the first step
    spawnCount = 0;
    resetClouds();
}

int32_t ChirpyRunnerGame::jumpScaleQ8() const
{
    // ratio = current scroll speed / base scroll speed, in Q8 (256 == 1.0).
    const int32_t ratioQ8 = (speedSub * 256) / SPEED_BASE;
    // Feed only JUMP_SPEEDUP_PCT of the speed increase into the jump scale: k = 1 + (ratio-1)*pct.
    const int32_t kQ8 = 256 + ((ratioQ8 - 256) * JUMP_SPEEDUP_PCT) / 100;
    return kQ8 < 256 ? 256 : kQ8; // never slower than the base hop
}

void ChirpyRunnerGame::jump()
{
    if (!alive || !grounded)
        return;
    // Scale velocity by k and gravity by k*k: the apex height is unchanged (Chirpy still clears the
    // same buildings) but the air-time shrinks by k, so the clearing window tightens with the speed.
    const int32_t kQ8 = jumpScaleQ8();
    vy = -(JUMP_V * kQ8) / 256;
    jumpGravity = (GRAVITY * kQ8 * kQ8) / (256 * 256);
    if (jumpGravity < GRAVITY)
        jumpGravity = GRAVITY;
    grounded = false;
}

bool ChirpyRunnerGame::step()
{
    if (!alive)
        return false;

    scrollClouds(); // decorative background parallax

    // --- Chirpy vertical motion (gravity latched at launch, so the arc stays consistent mid-air) ---
    vy += jumpGravity;
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
    display->drawString(cx, y, "CHIRPY DASH");
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

#if GRAPHICS_TFT_COLORING_ENABLED
// Obstacle colour palette; the game logic advances the index every 10 spawns.
static uint16_t obstacleColor(uint8_t idx)
{
    using namespace graphics;
    switch (idx) {
    case 0:
        return TFTPalette::Red;
    case 1:
        return TFTPalette::Orange;
    case 2:
        return TFTPalette::Yellow;
    case 3:
        return TFTPalette::Magenta;
    case 4:
        return TFTPalette::Cyan;
    default:
        return TFTPalette::Blue;
    }
}
#endif

void ChirpyRunner::drawPlaying(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setColor(WHITE);
    display->setFont(FONT_SMALL);

    // Clouds drifting in the background (drawn first so everything else sits in front).
    for (uint8_t i = 0; i < ChirpyRunnerGame::cloudSlots(); i++) {
        const int16_t cxp = x + game.cloudX(i);
        const int16_t cyp = y + game.cloudY(i);
        display->fillRect(cxp + 2, cyp, 4, 1);
        display->fillRect(cxp + 1, cyp + 1, 6, 1);
        display->fillRect(cxp, cyp + 2, 8, 1);
#if GRAPHICS_TFT_COLORING_ENABLED
        graphics::registerTFTColorRegionDirect(cxp, cyp, 8, 3, graphics::TFTPalette::LightGray, graphics::getThemeBodyBg());
#endif
    }

    // Score (top-left).
    char buf[16];
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "Sc %lu", static_cast<unsigned long>(game.score()));
    display->drawString(x + 2, y, buf);

    // Ground line.
    display->drawLine(x, y + ChirpyRunnerGame::GROUND_Y, x + display->getWidth() - 1, y + ChirpyRunnerGame::GROUND_Y);

    // Obstacles drawn as little buildings: a solid tower with two columns of punched-out windows
    // (dark holes). On colour displays the walls cycle colour every 10 spawns and the windows glow
    // (they are the region's off-pixels, so they take the off-colour).
    for (uint8_t i = 0; i < ChirpyRunnerGame::obstacleSlots(); i++) {
        if (!game.obstacleActive(i))
            continue;
        const int16_t oh = game.obstacleH(i);
        const int16_t ow = game.obstacleW(i);
        const int16_t oxp = x + game.obstacleX(i);
        const int16_t oyp = y + ChirpyRunnerGame::GROUND_Y - oh;

        display->setColor(WHITE);
        display->fillRect(oxp, oyp, ow, oh);
        // Windows: 1px holes in the left and right columns, every other row, skipping the roof row
        // and the ground-floor rows so the tower reads as a building.
        display->setColor(BLACK);
        for (int16_t wy = oyp + 2; wy <= oyp + oh - 3; wy += 2) {
            display->fillRect(oxp + 1, wy, 1, 1);
            display->fillRect(oxp + ow - 2, wy, 1, 1);
        }
        display->setColor(WHITE);
#if GRAPHICS_TFT_COLORING_ENABLED
        graphics::registerTFTColorRegionDirect(oxp, oyp, ow, oh, obstacleColor(game.obstacleColorIndex(i)),
                                               graphics::TFTPalette::White); // lit windows
#endif
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
