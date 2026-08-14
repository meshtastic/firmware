#include "Tetris.h"

// ===========================================================================
// Pure TetrisGame logic (no display/FS dependencies; always compiled)
// ===========================================================================

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

// ===========================================================================
// Tetris adapter (display + persistence + mesh; BaseUI games build only)
// ===========================================================================

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "GamesModule.h"
#include "NodeDB.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/TFTColorRegions.h"
#include "graphics/TFTPalette.h"
#include "graphics/images.h"
#include "main.h"
#include <cstddef>
#include <cstring>

// ---------------------------------------------------------------------------
// Vertical pixel layout on a 128×64 OLED
//
//  Board occupies the left side of the screen:
//    x = col × CELL_PX   (col 0 at left edge)
//    y = row × CELL_PX   (row 0 at top edge)
//    10 cols × 4 px = 40 px wide
//    16 rows × 4 px = 64 px tall  (fills the full display height)
//
//  Score panel: x = SCORE_OX .. 127  (86 px wide)
//    Labels (SCR / LVL / NXT) + values + next-piece preview.
// ---------------------------------------------------------------------------
static constexpr int16_t CELL_PX = 4;

Tetris::Tetris()
{
    scores_.load();
}

int32_t Tetris::tickIntervalMs() const
{
    // Speed ramps with level: 600 ms base, 45 ms per level, floor 50 ms.
    int32_t iv = 600 - static_cast<int32_t>(game.level()) * 45;
    return iv < 50 ? 50 : iv;
}

void Tetris::handleInput(input_broker_event ev)
{
    switch (ev) {
    case INPUT_BROKER_UP:
        game.rotate();
        break;
    case INPUT_BROKER_LEFT:
        game.moveLeft();
        break;
    case INPUT_BROKER_RIGHT:
        game.moveRight();
        break;
    case INPUT_BROKER_DOWN:
        game.softDrop();
        break;
    case INPUT_BROKER_SELECT:
    case INPUT_BROKER_SELECT_LONG:
        game.hardDrop();
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

#if GRAPHICS_TFT_COLORING_ENABLED
// Classic tetromino colours, indexed by piece type (0..6 == I O T S Z J L). Native RGB565.
static uint16_t tetrominoColor(uint8_t type)
{
    using namespace graphics;
    switch (type) {
    case 0:
        return TFTPalette::Cyan; // I
    case 1:
        return TFTPalette::Yellow; // O
    case 2:
        return TFTPalette::Magenta; // T
    case 3:
        return TFTPalette::Green; // S
    case 4:
        return TFTPalette::Red; // Z
    case 5:
        return TFTPalette::Blue; // J
    case 6:
        return TFTPalette::Orange; // L
    default:
        return TFTPalette::White;
    }
}
#endif

void Tetris::drawAttract(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setColor(WHITE);
    const int16_t w = display->getWidth();
    const int16_t cx = x + w / 2;
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(cx, y, "T E T R I S");
    const int16_t logoX = x + (w - tetris_width) / 2;
    const int16_t logoY = y + 15;
    display->drawXbm(logoX, logoY, tetris_width, tetris_height, tetris);
#if GRAPHICS_TFT_COLORING_ENABLED
    // The logo glyph is a T tetromino -- tint it the T-piece colour on colour displays.
    graphics::registerTFTColorRegionDirect(logoX, logoY, tetris_width, tetris_height, tetrominoColor(2),
                                           graphics::getThemeBodyBg());
#endif
    char hi[32];
    if (scores_.scoreAt(0) > 0 && scores_.nameAt(0)[0] != '\0')
        snprintf(hi, sizeof(hi), "High: %s %lu", scores_.nameAt(0), static_cast<unsigned long>(scores_.scoreAt(0)));
    else
        snprintf(hi, sizeof(hi), "High: %lu", static_cast<unsigned long>(scores_.scoreAt(0)));
    display->drawString(cx, y + 34, hi);
}

void Tetris::drawPlaying(OLEDDisplay *display, int16_t x, int16_t y)
{
    // Centered vertical layout:
    //   board: 10 cols × CELL_PX wide, fills display height (BOARD_ROWS × CELL_PX)
    //   left panel  (NXT preview) : x = 0 .. ox-2
    //   right panel (SCR / LVL)   : x = ox+boardW+1 .. display.width-1
    const int16_t boardW = TetrisGame::BOARD_COLS * CELL_PX;   // 40
    const int16_t ox = x + (display->getWidth() - boardW) / 2; // horizontal centre
    const int16_t oy = y;

    display->setColor(WHITE);

    // Separator lines either side of the board, plus bottom wall.
    display->drawLine(ox - 1, oy, ox - 1, oy + display->getHeight() - 1);
    display->drawLine(ox + boardW, oy, ox + boardW, oy + display->getHeight() - 1);
    display->drawLine(ox - 1, oy + display->getHeight() - 1, ox + boardW, oy + display->getHeight() - 1);

    // Cell helper.
    auto drawCell = [&](int8_t col, int8_t row) {
        if (col < 0 || row < 0 || col >= TetrisGame::BOARD_COLS || row >= TetrisGame::BOARD_ROWS)
            return;
        display->fillRect(ox + static_cast<int16_t>(col) * CELL_PX, oy + static_cast<int16_t>(row) * CELL_PX, CELL_PX - 1,
                          CELL_PX - 1);
    };

    // Locked cells.
    for (uint8_t r = 0; r < TetrisGame::BOARD_ROWS; r++)
        for (uint8_t c = 0; c < TetrisGame::BOARD_COLS; c++)
            if (game.board[r][c])
                drawCell(static_cast<int8_t>(c), static_cast<int8_t>(r));

    // Ghost piece - hollow outline.
    const TetrisGame::Piece &cur = game.current();
    const int8_t ghostR = game.ghostRow();
    if (ghostR != cur.row) {
        for (uint8_t pr = 0; pr < 4; pr++) {
            for (uint8_t pc = 0; pc < 4; pc++) {
                if (!TetrisGame::pieceCell(cur.type, cur.rot, pr, pc))
                    continue;
                const int8_t gc = static_cast<int8_t>(cur.col + pc);
                const int8_t gr = static_cast<int8_t>(ghostR + pr);
                if (gc < 0 || gr < 0 || gc >= TetrisGame::BOARD_COLS || gr >= TetrisGame::BOARD_ROWS)
                    continue;
                display->setPixel(ox + static_cast<int16_t>(gc) * CELL_PX + 1, oy + static_cast<int16_t>(gr) * CELL_PX + 1);
            }
        }
    }

    // Active piece - filled.
    for (uint8_t pr = 0; pr < 4; pr++) {
        for (uint8_t pc = 0; pc < 4; pc++) {
            if (!TetrisGame::pieceCell(cur.type, cur.rot, pr, pc))
                continue;
            drawCell(static_cast<int8_t>(cur.col + pc), static_cast<int8_t>(cur.row + pr));
        }
    }

    // --- Right panel: SCR and LVL ---
    const int16_t rpx = ox + boardW + 2;
    char buf[12];
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(rpx, y + 2, "SCR");
    snprintf(buf, sizeof(buf), "%lu", static_cast<unsigned long>(game.score()));
    display->drawString(rpx, y + 2 + FONT_HEIGHT_SMALL, buf);
    display->drawString(rpx, y + 2 + FONT_HEIGHT_SMALL * 2 + 2, "LVL");
    snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(game.level()));
    display->drawString(rpx, y + 2 + FONT_HEIGHT_SMALL * 3 + 2, buf);

    // --- Left panel: NXT (next piece preview) centred in the panel ---
    const int16_t lpanelW = ox - 2;       // pixels available left of board separator
    static constexpr int16_t PREV_PX = 3; // px per preview cell
    const int16_t previewW = 4 * PREV_PX; // 12 px
    const int16_t lpx = x + (lpanelW - previewW) / 2;
    const int16_t nxtLabelY = y + 2;
    const int16_t nxtPreviewY = nxtLabelY + FONT_HEIGHT_SMALL + 2;
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(x + lpanelW / 2, nxtLabelY, "NXT");
    const TetrisGame::Piece &nxt = game.next();
    for (uint8_t pr = 0; pr < 4; pr++)
        for (uint8_t pc = 0; pc < 4; pc++)
            if (TetrisGame::pieceCell(nxt.type, nxt.rot, pr, pc))
                display->fillRect(lpx + static_cast<int16_t>(pc) * PREV_PX, nxtPreviewY + static_cast<int16_t>(pr) * PREV_PX,
                                  PREV_PX - 1, PREV_PX - 1);

#if GRAPHICS_TFT_COLORING_ENABLED
    // On a colour display (e.g. HUB75), tint every block with its tetromino colour. The mono buffer
    // still carries the block pixels drawn above; registering a colour region over each run of
    // same-colour cells makes those "on" pixels render in colour instead of the theme foreground.
    // Runs are merged horizontally per row to stay within the region budget, and empty cells cost
    // nothing, so the region count only grows with how full the board is.
    const uint16_t bg = graphics::getThemeBodyBg();

    // Combined colour grid: locked cells plus the falling piece (same colour source == type + 1).
    uint8_t cg[TetrisGame::BOARD_ROWS][TetrisGame::BOARD_COLS];
    for (uint8_t r = 0; r < TetrisGame::BOARD_ROWS; r++)
        for (uint8_t c = 0; c < TetrisGame::BOARD_COLS; c++)
            cg[r][c] = game.board[r][c];
    for (uint8_t pr = 0; pr < 4; pr++)
        for (uint8_t pc = 0; pc < 4; pc++) {
            if (!TetrisGame::pieceCell(cur.type, cur.rot, pr, pc))
                continue;
            const int br = cur.row + pr, bc = cur.col + pc;
            if (br >= 0 && br < TetrisGame::BOARD_ROWS && bc >= 0 && bc < TetrisGame::BOARD_COLS)
                cg[br][bc] = static_cast<uint8_t>(cur.type + 1);
        }
    for (uint8_t r = 0; r < TetrisGame::BOARD_ROWS; r++) {
        uint8_t c = 0;
        while (c < TetrisGame::BOARD_COLS) {
            const uint8_t v = cg[r][c];
            if (v == 0) {
                c++;
                continue;
            }
            const uint8_t c0 = c;
            while (c < TetrisGame::BOARD_COLS && cg[r][c] == v)
                c++;
            const int16_t rx = ox + static_cast<int16_t>(c0) * CELL_PX;
            const int16_t ry = oy + static_cast<int16_t>(r) * CELL_PX;
            const int16_t rw = static_cast<int16_t>(c - c0) * CELL_PX - 1; // span the run, drop the trailing gap
            graphics::registerTFTColorRegionDirect(rx, ry, rw, CELL_PX - 1, tetrominoColor(static_cast<uint8_t>(v - 1)), bg);
        }
    }
    // Next-piece preview: one region over the 4x4 grid tinted with its colour.
    graphics::registerTFTColorRegionDirect(lpx, nxtPreviewY, 4 * PREV_PX, 4 * PREV_PX, tetrominoColor(nxt.type), bg);
#endif
}

#endif // HAS_SCREEN && BASEUI_HAS_GAMES
