#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * Pure, self-contained Chirpy Runner game logic (a dino-runner / flappy-style side-scroller).
 *
 * No Arduino/display dependencies - designed to be unit-tested natively (see test/test_chirpy)
 * and reused by the ChirpyRunner adapter below without pulling in the display stack.
 *
 * Chirpy runs in place at a fixed x on the ground; obstacles scroll in from the right and the
 * player presses jump (SELECT) to hop over them. Gravity pulls Chirpy back down. Clearing an
 * obstacle scores a point and nudges the scroll speed up. A collision ends the run. Chirpy's
 * vertical motion is tracked in fixed-point sub-pixels (SUBPX per pixel); everything is integer
 * math and statically sized (no heap).
 */
class ChirpyRunnerGame
{
  public:
    // Playfield, in pixels (a standard 128x64 OLED in landscape).
    static constexpr int16_t BOARD_W = 128;
    static constexpr int16_t BOARD_H = 64;

    // Chirpy sprite box and fixed horizontal position; GROUND_Y is where his feet rest.
    static constexpr int16_t CHIRPY_W = 12;
    static constexpr int16_t CHIRPY_H = 16;
    static constexpr int16_t CHIRPY_X = 14;
    static constexpr int16_t GROUND_Y = BOARD_H - 2; // 62

    static constexpr uint8_t MAX_OBSTACLES = 4;
    static constexpr int16_t OBST_W = 6;
    static constexpr uint8_t OBST_COLOR_COUNT = 6; // obstacle colour advances every 10 spawns
    static constexpr uint8_t CLOUD_COUNT = 3;      // decorative background clouds

    /** (Re)start a run: Chirpy on the ground, no obstacles yet, score 0. `seed` drives the
     * xorshift32 RNG used for obstacle heights and spacing. */
    void reset(uint32_t seed);

    /** Advance one tick (gravity + scroll + spawn + collision). Returns true while the run is in
     * progress, false once Chirpy hits an obstacle. No-op returning false once dead. */
    bool step();

    /** Hop, if Chirpy is on the ground (single jump; ignored while airborne). */
    void jump();

    bool isPlaying() const { return alive; }
    uint32_t score() const { return points; }
    bool onGround() const { return grounded; }

    // --- Rendering accessors (board pixels) ---
    int16_t chirpyY() const { return static_cast<int16_t>(chirpyTop / SUBPX); } // sprite top
    static constexpr uint8_t obstacleSlots() { return MAX_OBSTACLES; }
    bool obstacleActive(uint8_t i) const { return i < MAX_OBSTACLES && obst[i].active; }
    int16_t obstacleX(uint8_t i) const { return static_cast<int16_t>(obst[i].xSub / SUBPX); }
    uint8_t obstacleW(uint8_t i) const { return obst[i].w; }
    uint8_t obstacleH(uint8_t i) const { return obst[i].h; }
    uint8_t obstacleColorIndex(uint8_t i) const { return obst[i].colorIdx; }
    static constexpr uint8_t cloudSlots() { return CLOUD_COUNT; }
    int16_t cloudX(uint8_t i) const { return static_cast<int16_t>(cloud[i].xSub / SUBPX); }
    int16_t cloudY(uint8_t i) const { return cloud[i].y; }

  private:
    static constexpr int32_t SUBPX = 16;          // fixed-point sub-pixels per pixel
    static constexpr int32_t GRAVITY = 7;         // downward accel (sub-px/step^2)
    static constexpr int32_t JUMP_V = 75;         // initial upward velocity on a hop (sub-px/step)
    static constexpr int32_t SPEED_BASE = 32;     // base scroll speed (sub-px/step == 2 px)
    static constexpr int32_t SPEED_INC = 2;       // speed added per point scored
    static constexpr uint32_t SPEED_CAP_PTS = 35; // score at which the speed ramp caps (== 5 px/step)
    // Obstacle spacing is measured in TICKS, not pixels, so the time between obstacles stays
    // constant as the scroll speed ramps up -- otherwise a fixed pixel gap collapses into fewer
    // ticks at high speed until it drops below the jump duration and clearing becomes impossible.
    // The min is kept just above the ~22-tick jump so obstacles come in a tight but landable rhythm.
    static constexpr int16_t GAP_STEPS_MIN = 23; // min ticks between spawns (> jump duration)
    static constexpr int16_t GAP_STEPS_MAX = 40; // max ticks between spawns

    static constexpr int8_t BUILDING_HEIGHTS[] = {8, 12, 16}; // three tiers of obstacle heights (pixels)

    static constexpr int32_t CLOUD_SPEED_SUB = 6; // background scroll speed (sub-px/step, slow parallax)
    static constexpr int16_t CLOUD_W = 8;         // cloud puff width (for off-screen wrap)

    struct Obstacle {
        int32_t xSub; // left edge, sub-pixels
        uint8_t w;
        uint8_t h;
        uint8_t colorIdx; // which colour batch (advances every 10 spawns)
        bool active;
        bool scored;
    };

    struct Cloud {
        int32_t xSub; // left edge, sub-pixels
        int16_t y;    // top, pixels
    };

    int32_t groundedTopSub() const { return static_cast<int32_t>(GROUND_Y - CHIRPY_H) * SUBPX; }
    uint32_t nextRandom();
    void spawnObstacle();
    int16_t pickGapSteps();
    void resetClouds();
    void scrollClouds();

    int32_t chirpyTop = 0; // sprite-top y, sub-pixels
    int32_t vy = 0;        // vertical velocity, sub-pixels/step
    bool grounded = true;

    Obstacle obst[MAX_OBSTACLES] = {};
    Cloud cloud[CLOUD_COUNT] = {};
    int32_t speedSub = SPEED_BASE;
    int16_t spawnTimer = 0;  // ticks until the next obstacle spawns
    uint32_t spawnCount = 0; // total obstacles spawned (drives the colour cycle)

    uint32_t points = 0;
    uint32_t rng = 1; // xorshift32 state (never 0)
    bool alive = false;
};

#include "configuration.h"

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "Game.h"
#include "HighScoreTable.h"

/**
 * Chirpy Runner as a hosted Game. Wraps the pure logic above and supplies the attract art, the
 * side-scroller renderer (Chirpy sprite + obstacles + ground), the jump input, and its own
 * local high-score table. No mesh protocol (scores are local-only).
 */
class ChirpyRunner : public Game
{
  public:
    ChirpyRunner();

    const char *name() const override { return "Chirpy Dash"; }

    void start(uint32_t seed) override { game.reset(seed); }
    bool tick() override { return game.step(); }
    bool isPlaying() const override { return game.isPlaying(); }
    uint32_t score() const override { return game.score(); }
    int32_t tickIntervalMs() const override { return 33; } // ~30 fps; difficulty ramps via scroll speed

    void handleInput(input_broker_event ev) override;

    void drawAttract(OLEDDisplay *display, int16_t x, int16_t y) override;
    void drawPlaying(OLEDDisplay *display, int16_t x, int16_t y) override;

    HighScoreTableBase &scores() override { return scores_; }

  private:
    // On-disk high-score record. New file (magic 'CHRP', version 1); layout mirrors Snake's.
    struct ChirpyEntry {
        uint32_t score;
        uint32_t nodeNum;
        char shortName[5]; // three characters, NUL-terminated
        uint32_t epoch;    // getValidTime(), 0 if no RTC
    } __attribute__((packed));

    ChirpyRunnerGame game;
    HighScoreTable<ChirpyEntry> scores_{"/prefs/chirpy.dat", 0x43485250u, 1, "Chirpy"};
};

#endif // HAS_SCREEN && BASEUI_HAS_GAMES
