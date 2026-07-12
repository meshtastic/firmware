#include "Snake.h"

// ===========================================================================
// Pure SnakeGame logic (no display/FS dependencies; always compiled)
// ===========================================================================

void SnakeGame::reset(uint32_t seed)
{
    memset(occ, 0, sizeof(occ));
    len = 0;
    points = 0;
    alive = true;
    won = false;
    rng = seed ? seed : 0xA5A5A5A5u; // xorshift32 must never be seeded with 0

    // Spawn a START_LEN snake horizontally in the middle of the board, heading right, with the
    // head at the centre and the body trailing to its left.
    const uint8_t cx = GRID_W / 2;
    const uint8_t cy = GRID_H / 2;
    dir = DIR_RIGHT;
    pendingDir = DIR_RIGHT;
    tailIdx = 0;
    for (uint8_t i = 0; i < START_LEN; i++) {
        const uint8_t x = static_cast<uint8_t>(cx - (START_LEN - 1) + i);
        body[i] = {x, cy};
        setOcc(cellIndex(x, cy));
        len++;
    }
    headIdx = static_cast<uint16_t>(START_LEN - 1);

    placeFood();
}

bool SnakeGame::isReverse(Direction a, Direction b)
{
    return (a == DIR_UP && b == DIR_DOWN) || (a == DIR_DOWN && b == DIR_UP) || (a == DIR_LEFT && b == DIR_RIGHT) ||
           (a == DIR_RIGHT && b == DIR_LEFT);
}

bool SnakeGame::setDirection(Direction d)
{
    if (isReverse(dir, d))
        return false;
    pendingDir = d;
    return true;
}

uint32_t SnakeGame::nextRandom()
{
    uint32_t x = rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng = x;
    return x;
}

bool SnakeGame::placeFood()
{
    const uint16_t free = static_cast<uint16_t>(CELL_COUNT - len);
    if (free == 0)
        return false; // board full -> caller treats as a win

    // Pick the k-th free cell (k uniform in [0, free)) via a single linear scan. Deterministic,
    // always valid, and cheap for a 384-cell board -- no rejection sampling / near-full special case.
    uint16_t k = static_cast<uint16_t>(nextRandom() % free);
    for (uint16_t idx = 0; idx < CELL_COUNT; idx++) {
        if (!getOcc(idx)) {
            if (k == 0) {
                foodCell = {static_cast<uint8_t>(idx % GRID_W), static_cast<uint8_t>(idx / GRID_W)};
                return true;
            }
            k--;
        }
    }
    return false; // unreachable while free > 0
}

bool SnakeGame::step()
{
    if (!alive)
        return false;

    dir = pendingDir; // commit the latched heading

    const Cell h = body[headIdx];
    int16_t nx = h.x;
    int16_t ny = h.y;
    switch (dir) {
    case DIR_UP:
        ny--;
        break;
    case DIR_DOWN:
        ny++;
        break;
    case DIR_LEFT:
        nx--;
        break;
    case DIR_RIGHT:
        nx++;
        break;
    }

    // Wall collision (signed math catches the 0-- underflow too).
    if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) {
        alive = false;
        return false;
    }

    const uint16_t nidx = cellIndex(static_cast<uint8_t>(nx), static_cast<uint8_t>(ny));
    const bool eating = (nx == foodCell.x && ny == foodCell.y);

    // When not eating the tail vacates this tick, so free it first: moving into the cell the
    // tail is leaving is legal (classic snake), moving into any other body cell is fatal.
    if (!eating) {
        clearOcc(cellIndex(body[tailIdx].x, body[tailIdx].y));
        tailIdx = static_cast<uint16_t>((tailIdx + 1) % CAP);
        len--;
    }

    if (getOcc(nidx)) {
        alive = false;
        return false;
    }

    // Advance the head into the new cell.
    headIdx = static_cast<uint16_t>((headIdx + 1) % CAP);
    body[headIdx] = {static_cast<uint8_t>(nx), static_cast<uint8_t>(ny)};
    setOcc(nidx);
    len++;

    if (eating) {
        points++;
        if (!placeFood()) {
            won = true;
            alive = false; // board completely filled -- the player won
        }
    }

    return alive;
}

// ===========================================================================
// Snake adapter (display + persistence; BaseUI games build only)
// ===========================================================================

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/TFTColorRegions.h"
#include "graphics/TFTPalette.h"
#include "graphics/images.h"
#include "main.h"
#if SNAKE_ANNOUNCE_HIGH_SCORE
#include "GamesModule.h"
#include "MeshService.h"
#include "mesh/NodeDB.h"
#endif

// --- Pixel layout on a 128x64 OLED -----------------------------------------------------------
// Each cell is CELL_PX square. The GRID_W x GRID_H playfield (128x48) is bottom-aligned, leaving
// a SCORE_H-pixel score bar across the top. On taller displays the board bottom-aligns and
// centres horizontally; screen edges are the walls.
static constexpr int16_t CELL_PX = 4;
static constexpr int16_t SCORE_H = 16;

Snake::Snake()
{
    scores_.load();
}

int32_t Snake::tickIntervalMs() const
{
    // Speed ramps up as the snake grows: 160 ms base, down to a 70 ms floor.
    int32_t iv = 160 - static_cast<int32_t>(game.length()) * 3;
    return iv < 70 ? 70 : iv;
}

void Snake::handleInput(input_broker_event ev)
{
    switch (ev) {
    case INPUT_BROKER_UP:
        game.setDirection(SnakeGame::DIR_UP);
        break;
    case INPUT_BROKER_DOWN:
        game.setDirection(SnakeGame::DIR_DOWN);
        break;
    case INPUT_BROKER_LEFT:
        game.setDirection(SnakeGame::DIR_LEFT);
        break;
    case INPUT_BROKER_RIGHT:
        game.setDirection(SnakeGame::DIR_RIGHT);
        break;
    default:
        break;
    }
}

void Snake::drawAttract(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setColor(WHITE);
    const int16_t w = display->getWidth();
    const int16_t cx = x + w / 2;
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(cx, y, "S N A K E");
    // Snake glyph, centred below the title.
    const int16_t logoX = x + (w - snake_width) / 2;
    const int16_t logoY = y + 15;
    display->drawXbm(logoX, logoY, snake_width, snake_height, snake);
#if GRAPHICS_TFT_COLORING_ENABLED
    // On a colour display, paint the snake green with a red tongue. The forked tongue is the pair
    // of pixels poking out past the body on the right edge (~row 6 of the 16x16 glyph); a red
    // region registered after the green one wins where they overlap, so the body stays green.
    const uint16_t bg = graphics::getThemeBodyBg();
    graphics::registerTFTColorRegionDirect(logoX, logoY, snake_width, snake_height, graphics::TFTPalette::Green, bg);
    graphics::registerTFTColorRegionDirect(logoX + 13, logoY + 5, 3, 4, graphics::TFTPalette::Red, bg);
#endif
    char hi[32];
    if (scores_.scoreAt(0) > 0 && scores_.nameAt(0)[0] != '\0')
        snprintf(hi, sizeof(hi), "High: %s %lu", scores_.nameAt(0), static_cast<unsigned long>(scores_.scoreAt(0)));
    else
        snprintf(hi, sizeof(hi), "High: %lu", static_cast<unsigned long>(scores_.scoreAt(0)));
    display->drawString(cx, y + 34, hi);
}

void Snake::drawPlaying(OLEDDisplay *display, int16_t x, int16_t y)
{
    char buf[24];
    display->setColor(WHITE);
    display->setFont(FONT_SMALL);

    // Score bar: current score on the left, best on the right.
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "Score %lu", static_cast<unsigned long>(game.score()));
    display->drawString(x + 2, y + 2, buf);
    if (scores_.scoreAt(0) > 0) {
        display->setTextAlignment(TEXT_ALIGN_RIGHT);
        snprintf(buf, sizeof(buf), "Hi %lu", static_cast<unsigned long>(scores_.scoreAt(0)));
        display->drawString(x + display->getWidth() - 2, y + 2, buf);
    }
    display->drawLine(x, y + SCORE_H - 1, x + display->getWidth() - 1, y + SCORE_H - 1);

    // Board is bottom-aligned; centre it horizontally.
    const int16_t boardW = SnakeGame::GRID_W * CELL_PX;
    const int16_t boardH = SnakeGame::GRID_H * CELL_PX;
    const int16_t ox = x + (display->getWidth() - boardW) / 2;
    const int16_t oy = y + display->getHeight() - boardH;

    for (uint16_t i = 0; i < game.length(); i++) {
        SnakeGame::Cell c = game.bodyAt(i);
        display->fillRect(ox + c.x * CELL_PX, oy + c.y * CELL_PX, CELL_PX, CELL_PX);
    }
    // Food: a 2x2 dot centred in its cell.
    SnakeGame::Cell f = game.food();
    display->fillRect(ox + f.x * CELL_PX + 1, oy + f.y * CELL_PX + 1, 2, 2);

#if GRAPHICS_TFT_COLORING_ENABLED
    // On a colour display, paint the whole snake green, then the apple red on top. One region over
    // the board tints every lit body cell green (the snake can be too long for per-cell regions);
    // a red region over the food pixel wins where it overlaps.
    const uint16_t bg = graphics::getThemeBodyBg();
    graphics::registerTFTColorRegionDirect(ox, oy, boardW, boardH, graphics::TFTPalette::MeshtasticGreen, bg);
    graphics::registerTFTColorRegionDirect(ox + f.x * CELL_PX + 1, oy + f.y * CELL_PX + 1, 2, 2, graphics::TFTPalette::Red, bg);
#endif
}

#if SNAKE_ANNOUNCE_HIGH_SCORE
void Snake::onNewHighScore(GamesModule &host, const char *initials, uint32_t score, bool isNewTop)
{
    if (!isNewTop || score == 0 || !service)
        return;
    meshtastic_MeshPacket *p = host.gameAllocDataPacket();
    p->to = NODENUM_BROADCAST;
    p->channel = 0; // primary channel for v1
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    // ASCII only -- avoids tofu if a receiving node's font lacks an emoji glyph.
    p->decoded.payload.size =
        snprintf(reinterpret_cast<char *>(p->decoded.payload.bytes), sizeof(p->decoded.payload.bytes),
                 "%s set a new Snake high score: %lu", (initials && initials[0]) ? initials : owner.short_name,
                 static_cast<unsigned long>(score));
    service->sendToMesh(p);
    LOG_INFO("Snake: announced new high score %lu", static_cast<unsigned long>(score));
}
#endif

#endif // HAS_SCREEN && BASEUI_HAS_GAMES
