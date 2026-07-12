#pragma once

#include "configuration.h"

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "Game.h"
#include "HighScoreTable.h"
#include "SnakeGame.h"

// High-score mesh announcement is a COMPILE-TIME option, default OFF. Broadcasting to shared
// airtime must be opted into at build time with -DSNAKE_ANNOUNCE_HIGH_SCORE=1; when disabled
// (the default) the announcement code is compiled out entirely -- there is no runtime toggle.
#ifndef SNAKE_ANNOUNCE_HIGH_SCORE
#define SNAKE_ANNOUNCE_HIGH_SCORE 0
#endif

/**
 * Snake as a hosted Game. Wraps the pure SnakeGame logic and supplies the attract art, the
 * playfield renderer, the direction input, the length-based speed curve, and its own high-score
 * table. Optionally announces a new all-time #1 as a plain text message (compile-time gated).
 */
class Snake : public Game
{
  public:
    Snake();

    const char *name() const override { return "Snake"; }

    void start(uint32_t seed) override { game.reset(seed); }
    bool tick() override { return game.step(); }
    bool isPlaying() const override { return game.isPlaying(); }
    uint32_t score() const override { return game.score(); }
    int32_t tickIntervalMs() const override;

    void handleInput(input_broker_event ev) override;

    void drawAttract(OLEDDisplay *display, int16_t x, int16_t y) override;
    void drawPlaying(OLEDDisplay *display, int16_t x, int16_t y) override;

    HighScoreTableBase &scores() override { return scores_; }

#if SNAKE_ANNOUNCE_HIGH_SCORE
    void onNewHighScore(GamesModule &host, const char *initials, uint32_t score, bool isNewTop) override;
#endif

  private:
    // On-disk high-score record; layout preserved from the original SnakeModule so snake.dat keeps
    // loading. Magic 'SNEK', file version 1.
    struct SnakeEntry {
        uint32_t score;
        uint32_t nodeNum;
        char shortName[5]; // three characters, NUL-terminated
        uint32_t epoch;    // getValidTime(), 0 if no RTC
    } __attribute__((packed));

    SnakeGame game;
    HighScoreTable<SnakeEntry> scores_{"/prefs/snake.dat", 0x534E454Bu, 1, "Snake"};
};

#endif // HAS_SCREEN && BASEUI_HAS_GAMES
