#include "TetrisGame.h"

// ---------------------------------------------------------------------------
// Shape table
//
// SHAPES[type][rot][row] encodes each row of the 4×4 bounding box as a 4-bit
// column bitmask (bit 0 = leftmost column).  All seven SRS tetrominoes in their
// four CW rotations.
//
//  Type 0  I    Type 1  O    Type 2  T    Type 3  S
//  Type 4  Z    Type 5  J    Type 6  L
// ---------------------------------------------------------------------------
const uint8_t TetrisGame::SHAPES[PIECE_TYPES][4][4] = {
    // I  ----  rot0: .XXXX  rot1: ..X..  rot2: .....  rot3: .X...
    {{0, 15, 0, 0}, {4, 4, 4, 4}, {0, 0, 15, 0}, {2, 2, 2, 2}},
    // O  ----  same all rotations
    {{6, 6, 0, 0}, {6, 6, 0, 0}, {6, 6, 0, 0}, {6, 6, 0, 0}},
    // T  ----
    {{2, 7, 0, 0}, {2, 6, 2, 0}, {0, 7, 2, 0}, {2, 3, 2, 0}},
    // S  ----
    {{6, 3, 0, 0}, {1, 3, 2, 0}, {0, 6, 3, 0}, {2, 6, 4, 0}},
    // Z  ----
    {{3, 6, 0, 0}, {2, 3, 1, 0}, {0, 3, 6, 0}, {4, 6, 2, 0}},
    // J  ----
    {{1, 7, 0, 0}, {6, 2, 2, 0}, {0, 7, 4, 0}, {2, 2, 3, 0}},
    // L  ----
    {{4, 7, 0, 0}, {2, 2, 6, 0}, {0, 7, 1, 0}, {3, 2, 2, 0}},
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

bool TetrisGame::pieceCell(uint8_t type, uint8_t rot, uint8_t pr, uint8_t pc)
{
    if (type >= PIECE_TYPES || rot >= 4 || pr >= 4 || pc >= 4)
        return false;
    return (SHAPES[type][rot][pr] >> pc) & 1u;
}

uint32_t TetrisGame::nextRandom()
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng = x;
    return x;
}

TetrisGame::Piece TetrisGame::spawnPiece(uint8_t type) const
{
    // Centre horizontally in the 10-wide board; bounding box starts at col 3.
    Piece p;
    p.type = type;
    p.rot = 0;
    p.col = static_cast<int8_t>((BOARD_COLS - 4) / 2); // 3 for a 10-wide board
    p.row = 0;
    return p;
}

bool TetrisGame::canPlace(const Piece &p) const
{
    for (uint8_t pr = 0; pr < 4; pr++) {
        for (uint8_t pc = 0; pc < 4; pc++) {
            if (!pieceCell(p.type, p.rot, pr, pc))
                continue;
            const int16_t br = static_cast<int16_t>(p.row) + pr;
            const int16_t bc = static_cast<int16_t>(p.col) + pc;
            if (br < 0)
                continue; // above the board - allowed during spawn
            if (br >= BOARD_ROWS || bc < 0 || bc >= BOARD_COLS)
                return false;
            if (board[br][bc] != 0)
                return false;
        }
    }
    return true;
}

void TetrisGame::lockPiece()
{
    for (uint8_t pr = 0; pr < 4; pr++) {
        for (uint8_t pc = 0; pc < 4; pc++) {
            if (!pieceCell(cur.type, cur.rot, pr, pc))
                continue;
            const int16_t br = static_cast<int16_t>(cur.row) + pr;
            const int16_t bc = static_cast<int16_t>(cur.col) + pc;
            if (br >= 0 && br < BOARD_ROWS && bc >= 0 && bc < BOARD_COLS)
                board[br][bc] = static_cast<uint8_t>(cur.type + 1); // colour 1..7
        }
    }
}

int TetrisGame::clearLines()
{
    int cleared = 0;
    for (int r = BOARD_ROWS - 1; r >= 0;) {
        bool full = true;
        for (int c = 0; c < BOARD_COLS && full; c++) {
            if (board[r][c] == 0)
                full = false;
        }
        if (full) {
            // Shift every row above down by one.
            for (int rr = r; rr > 0; rr--)
                memcpy(board[rr], board[rr - 1], BOARD_COLS);
            memset(board[0], 0, BOARD_COLS);
            cleared++;
            // Recheck same index - it now contains the row that was above.
        } else {
            r--;
        }
    }
    return cleared;
}

void TetrisGame::advanceNext()
{
    lockPiece();

    const int cleared = clearLines();
    if (cleared > 0) {
        lines += static_cast<uint16_t>(cleared);
        // Nintendo-style line-clear scoring (×level).
        static const uint16_t LINE_PTS[5] = {0, 100, 300, 500, 800};
        pts += LINE_PTS[cleared < 5 ? cleared : 4] * lvl;
        // Level up every 10 lines, cap at 20.
        const uint8_t newLvl = static_cast<uint8_t>(lines / 10 + 1);
        lvl = newLvl > 20 ? 20 : newLvl;
    }

    // nxt becomes the active piece; generate a fresh nxt.
    cur = nxt;
    if (!canPlace(cur)) {
        alive = false;
        return;
    }
    nxt = spawnPiece(static_cast<uint8_t>(nextRandom() % PIECE_TYPES));
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void TetrisGame::reset(uint32_t seed)
{
    memset(board, 0, sizeof(board));
    pts = 0;
    lvl = 1;
    lines = 0;
    alive = true;
    rng = seed ? seed : 0xA5A5A5A5u;
    cur = spawnPiece(static_cast<uint8_t>(nextRandom() % PIECE_TYPES));
    nxt = spawnPiece(static_cast<uint8_t>(nextRandom() % PIECE_TYPES));
}

bool TetrisGame::moveLeft()
{
    Piece p = cur;
    p.col--;
    if (!canPlace(p))
        return false;
    cur = p;
    return true;
}

bool TetrisGame::moveRight()
{
    Piece p = cur;
    p.col++;
    if (!canPlace(p))
        return false;
    cur = p;
    return true;
}

bool TetrisGame::rotate()
{
    Piece p = cur;
    p.rot = static_cast<uint8_t>((p.rot + 1) % 4);
    if (canPlace(p)) {
        cur = p;
        return true;
    }
    // Wall-kick: try ±1, ±2 column offsets.
    const int8_t kicks[] = {-1, 1, -2, 2};
    for (int8_t kick : kicks) {
        Piece q = p;
        q.col = static_cast<int8_t>(p.col + kick);
        if (canPlace(q)) {
            cur = q;
            return true;
        }
    }
    return false;
}

bool TetrisGame::softDrop()
{
    Piece p = cur;
    p.row++;
    if (!canPlace(p)) {
        advanceNext(); // locks cur, clears lines, spawns next; may set alive=false
        return false;
    }
    cur = p;
    return true;
}

void TetrisGame::hardDrop()
{
    const int8_t land = ghostRow();
    const uint32_t dropped = static_cast<uint32_t>(land - cur.row);
    pts += dropped * 2;
    cur.row = land;
    advanceNext();
}

int8_t TetrisGame::ghostRow() const
{
    Piece p = cur;
    while (true) {
        Piece q = p;
        q.row++;
        if (!canPlace(q))
            break;
        p = q;
    }
    return p.row;
}

bool TetrisGame::step()
{
    if (!alive)
        return false;
    softDrop(); // may lock and advance, potentially setting alive=false
    return alive;
}
