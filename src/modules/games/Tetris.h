#pragma once

#include "configuration.h"

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "Game.h"
#include "HighScoreTable.h"
#include "TetrisGame.h"

// High-score mesh announcement is a COMPILE-TIME option, default OFF.
#ifndef TETRIS_ANNOUNCE_HIGH_SCORE
#define TETRIS_ANNOUNCE_HIGH_SCORE 0
#endif

/**
 * Tetris as a hosted Game. Wraps the pure TetrisGame logic and supplies the attract art, the
 * portrait playfield renderer, the rotate/move/drop input, the level-based speed curve, and its
 * own high-score table.
 *
 * Optionally (compile-time -DTETRIS_ANNOUNCE_HIGH_SCORE=1) it speaks a small binary mesh protocol
 * to share scores between nodes: single-score and full-table broadcasts, plus a receive/merge
 * path. Wire packets carry a game_id byte ('T') so Snake and Tetris packets are never confused
 * (Tetris wire sizes are 11 / 68 bytes; Snake's are 10 / 67). The receive/merge path is compiled
 * in unconditionally; only the senders are gated by the flag.
 */
class Tetris : public Game
{
  public:
    Tetris();

    const char *name() const override { return "Tetris"; }

    void start(uint32_t seed) override { game.reset(seed); }
    bool tick() override { return game.step(); }
    bool isPlaying() const override { return game.isPlaying(); }
    uint32_t score() const override { return game.score(); }
    int32_t tickIntervalMs() const override;

    void handleInput(input_broker_event ev) override;

    void drawAttract(OLEDDisplay *display, int16_t x, int16_t y) override;
    void drawPlaying(OLEDDisplay *display, int16_t x, int16_t y) override;
    const char *gameOverHint() const override { return "SEL: scores  BCK: exit"; }

    HighScoreTableBase &scores() override { return scores_; }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
#if TETRIS_ANNOUNCE_HIGH_SCORE
    bool wantsPeriodicMesh() const override { return true; }
    int32_t meshTick(GamesModule &host) override;
    void onNewHighScore(GamesModule &host, const char *initials, uint32_t score, bool isNewTop) override;
#endif

  private:
    // On-disk high-score record; layout preserved from the original TetrisModule so tetris.dat
    // keeps loading. Magic 'TETR', file version 1.
    struct TetrisEntry {
        uint32_t score;
        char shortName[5]; // NUL-terminated 3-char display name
        uint32_t nodeNum;
        uint32_t epoch;
    } __attribute__((packed));

    TetrisGame game;
    HighScoreTable<TetrisEntry> scores_{"/prefs/tetris.dat", 0x54455452u, 1, "Tetris"};

#if TETRIS_ANNOUNCE_HIGH_SCORE
    uint32_t lastBroadcastMs = 0;
    int32_t nextBroadcastIntervalMs() const;
    void broadcastAllScores(GamesModule &host);
#endif
};

#endif // HAS_SCREEN && BASEUI_HAS_GAMES
