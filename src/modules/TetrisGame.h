#pragma once

#include <stdint.h>
#include <string.h>

/**
 * Pure, self-contained Tetris game logic.
 *
 * No Arduino/display dependencies - designed to be unit-tested natively and
 * reused by TetrisModule without pulling in the display stack.
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
