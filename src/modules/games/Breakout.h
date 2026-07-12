#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/**
 * Pure, self-contained Breakout game logic.
 *
 * No Arduino/display dependencies - designed to be unit-tested natively (see test/test_breakout)
 * and reused by the Breakout adapter below without pulling in the display stack.
 *
 * The board is a fixed BOARD_W x BOARD_H pixel field. A grid of bricks sits near the top, a paddle
 * slides along the bottom, and a ball bounces between them. Ball position/velocity are tracked in
 * fixed-point sub-pixels (SUBPX per pixel) so movement is smooth and deterministic; everything is
 * integer math and statically sized (no heap).
 */
class BreakoutGame
{
  public:
    // Playfield, in pixels (a standard 128x64 OLED in landscape).
    static constexpr int16_t BOARD_W = 128;
    static constexpr int16_t BOARD_H = 64;

    // Brick grid.
    static constexpr uint8_t BRICK_COLS = 8;
    static constexpr uint8_t BRICK_ROWS = 5;
    static constexpr int16_t BRICK_W = BOARD_W / BRICK_COLS; // 16
    static constexpr int16_t BRICK_H = 4;
    static constexpr int16_t BRICK_TOP = 12; // top of the first course; leaves room for the score row

    // Paddle.
    static constexpr int16_t PADDLE_W = 24;
    static constexpr int16_t PADDLE_H = 2;
    static constexpr int16_t PADDLE_Y = BOARD_H - 3; // top edge of the paddle
    static constexpr int16_t PADDLE_STEP = 6;        // pixels moved per input event

    static constexpr uint8_t START_LIVES = 3;
    static constexpr uint8_t POINTS_PER_BRICK = 10;

    /** (Re)start a full game: rebuild bricks, reset lives/score, serve the ball. `seed` drives the
     * xorshift32 RNG used for the initial serve direction. */
    void reset(uint32_t seed);

    /** Advance the simulation by one tick (move the ball, resolve collisions). Returns true if the
     * game is still in progress, false once the last life is lost. No-op returning false once dead. */
    bool step();

    /** Slide the paddle one step; the ball is unaffected. */
    void moveLeft();
    void moveRight();

    bool isPlaying() const { return alive; }
    uint32_t score() const { return points; }
    uint8_t lives() const { return livesLeft; }
    uint8_t level() const { return levelNum; }
    uint16_t bricksRemaining() const { return bricksLeft; }

    // --- Rendering accessors (all in board pixels) ---
    int16_t paddleX() const { return paddleLeft; }
    int16_t ballX() const { return static_cast<int16_t>(ballPxX / SUBPX); }
    int16_t ballY() const { return static_cast<int16_t>(ballPxY / SUBPX); }
    bool brickAt(uint8_t row, uint8_t col) const { return row < BRICK_ROWS && col < BRICK_COLS && bricks[row][col]; }

  private:
    static constexpr int32_t SUBPX = 16;    // fixed-point sub-pixels per pixel
    static constexpr int32_t BALL_VY = 40;  // vertical ball speed (sub-pixels/step)
    static constexpr int16_t BALL_SIZE = 2; // ball is drawn BALL_SIZE x BALL_SIZE

    void buildBricks();
    void serveBall();
    void nextLevel();
    uint32_t nextRandom();

    uint8_t bricks[BRICK_ROWS][BRICK_COLS] = {0}; // 0 == cleared, 1 == present
    uint16_t bricksLeft = 0;

    int16_t paddleLeft = 0; // left edge of the paddle, in pixels

    int32_t ballPxX = 0, ballPxY = 0; // ball centre, in sub-pixels
    int32_t ballVx = 0, ballVy = 0;   // ball velocity, in sub-pixels/step

    uint32_t points = 0;
    uint8_t livesLeft = 0;
    uint8_t levelNum = 1;
    uint32_t rng = 1; // xorshift32 state (never 0)
    bool alive = false;
};

#include "configuration.h"

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "Game.h"
#include "HighScoreTable.h"

/**
 * Breakout as a hosted Game. Wraps the pure BreakoutGame logic above and supplies the attract art,
 * the playfield renderer, the paddle input, the level-based speed curve, and its own local
 * high-score table. No mesh protocol (scores are local-only).
 */
class Breakout : public Game
{
  public:
    Breakout();

    const char *name() const override { return "Breakout"; }

    void start(uint32_t seed) override { game.reset(seed); }
    bool tick() override { return game.step(); }
    bool isPlaying() const override { return game.isPlaying(); }
    uint32_t score() const override { return game.score(); }
    int32_t tickIntervalMs() const override;

    void handleInput(input_broker_event ev) override;

    void drawAttract(OLEDDisplay *display, int16_t x, int16_t y) override;
    void drawPlaying(OLEDDisplay *display, int16_t x, int16_t y) override;

    HighScoreTableBase &scores() override { return scores_; }

  private:
    // On-disk high-score record. New file (magic 'BRKT', version 1); layout mirrors Snake's.
    struct BreakoutEntry {
        uint32_t score;
        uint32_t nodeNum;
        char shortName[5]; // three characters, NUL-terminated
        uint32_t epoch;    // getValidTime(), 0 if no RTC
    } __attribute__((packed));

    BreakoutGame game;
    HighScoreTable<BreakoutEntry> scores_{"/prefs/breakout.dat", 0x424B5254u, 1, "Breakout"};
};

#endif // HAS_SCREEN && BASEUI_HAS_GAMES
