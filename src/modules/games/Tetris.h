#pragma once

#include <stdint.h>
#include <string.h>

/**
 * Pure, self-contained Tetris game logic.
 *
 * No Arduino/display dependencies - designed to be unit-tested natively and reused by the Tetris
 * adapter below without pulling in the display stack.
 *
 * Board coordinate system: col=0 is leftmost, row=0 is top (gravity goes toward higher rows).
 * board[row][col] holds 0 (empty) or 1..7 (locked piece colour index).
 * No dynamic allocation: total struct size is ~260 bytes.
 */
class TetrisGame
{
  public:
    static constexpr uint8_t BOARD_COLS = 10;
    static constexpr uint8_t BOARD_ROWS = 16; // 16×4 px = 64 px - fills a standard 64px OLED
    static constexpr uint8_t PIECE_TYPES = 7; // I O T S Z J L

    struct Piece {
        int8_t col;   // left column of the 4×4 bounding box (may be negative during spawn)
        int8_t row;   // top row of the 4×4 bounding box (may be negative during spawn)
        uint8_t type; // 0..6
        uint8_t rot;  // 0..3
    };

    // board[row][col]: 0 = empty, 1..7 = locked piece colour
    uint8_t board[BOARD_ROWS][BOARD_COLS];

    /** Start (or restart) the game. seed drives the xorshift32 RNG. */
    void reset(uint32_t seed);

    /** Shift the current piece left one column. Returns true if it moved. */
    bool moveLeft();

    /** Shift the current piece right one column. Returns true if it moved. */
    bool moveRight();

    /**
     * Rotate the current piece CW. Tries a basic wall-kick (±1, ±2 column) if the
     * natural rotation overlaps a wall or locked cell. Returns true if it rotated.
     */
    bool rotate();

    /**
     * Move the current piece down one row. If it cannot fall it is locked,
     * lines are cleared, the next piece becomes current, and a new next is generated.
     * Returns false after locking (game may or may not be over).
     */
    bool softDrop();

    /**
     * Instantly drop the current piece to where it would land and lock it.
     * Awards 2 pts per row dropped.
     */
    void hardDrop();

    /**
     * Gravity tick: same as softDrop() - move down one row, lock if needed.
     * Returns true while the game is alive, false after game-over.
     */
    bool step();

    bool isPlaying() const { return alive; }
    uint32_t score() const { return pts; }
    uint8_t level() const { return lvl; }
    uint16_t linesCleared() const { return lines; }

    const Piece &current() const { return cur; }
    const Piece &next() const { return nxt; }

    /**
     * Returns the top row the current piece would occupy if instantly dropped.
     * Used by the renderer to show a ghost/shadow piece.
     */
    int8_t ghostRow() const;

    /**
     * Returns true if cell (pr, pc) within the 4×4 bounding box is filled for
     * the given piece type and rotation. Safe for any (type, rot, pr, pc).
     */
    static bool pieceCell(uint8_t type, uint8_t rot, uint8_t pr, uint8_t pc);

  private:
    Piece cur = {};
    Piece nxt = {};
    uint32_t pts = 0;
    uint8_t lvl = 1;
    uint16_t lines = 0;
    uint32_t rng = 1; // xorshift32 state - must never be 0
    bool alive = false;

    uint32_t nextRandom();
    Piece spawnPiece(uint8_t type) const;
    void advanceNext(); // lock cur, clear lines, shift nxt→cur, spawn new nxt
    bool canPlace(const Piece &p) const;
    void lockPiece();
    int clearLines(); // returns number of lines cleared (0..4)

    // Shape table: SHAPES[type][rot][row] = 4-bit column bitmask.
    // Bit 0 = leftmost column (col 0), bit 3 = rightmost (col 3) of the 4×4 box.
    static const uint8_t SHAPES[PIECE_TYPES][4][4];
};

#include "configuration.h"

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "Game.h"
#include "HighScoreTable.h"

// High-score mesh announcement is a COMPILE-TIME option, default OFF.
#ifndef TETRIS_ANNOUNCE_HIGH_SCORE
#define TETRIS_ANNOUNCE_HIGH_SCORE 0
#endif

/**
 * Tetris as a hosted Game. Wraps the pure TetrisGame logic above and supplies the attract art, the
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
