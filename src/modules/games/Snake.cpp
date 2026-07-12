#include "Snake.h"

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
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
    display->drawXbm(x + (w - snake_width) / 2, y + 15, snake_width, snake_height, snake);
    char hi[32];
    if (scores_.scoreAt(0) > 0 && scores_.nameAt(0)[0] != '\0')
        snprintf(hi, sizeof(hi), "High: %s %lu", scores_.nameAt(0), static_cast<unsigned long>(scores_.scoreAt(0)));
    else
        snprintf(hi, sizeof(hi), "High: %lu", static_cast<unsigned long>(scores_.scoreAt(0)));
    display->drawString(cx, y + 34, hi);
    display->drawString(cx, y + 48, "SELECT to play");
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
