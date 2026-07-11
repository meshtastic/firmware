#include "SnakeModule.h"

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "FSCommon.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "SPILock.h"
#include "SafeFile.h"
#include "TetrisModule.h"
#include "concurrency/LockGuard.h"
#include "gps/RTC.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/images.h"
#include "main.h"
#include <ErriezCRC32.h>
#include <cstddef>

SnakeModule *snakeModule;

// --- Pixel layout on a 128x64 OLED -----------------------------------------------------------
// Each cell is CELL_PX square. The GRID_W x GRID_H playfield (128x48) is bottom-aligned, leaving
// a SCORE_H-pixel score bar across the top. On taller displays the board bottom-aligns and
// centres horizontally; screen edges are the walls.
static constexpr int16_t CELL_PX = 4;
static constexpr int16_t SCORE_H = 16;

static constexpr const char *SNAKE_HS_FILE = "/prefs/snake.dat";

SnakeModule::SnakeModule() : SinglePortModule("snake", meshtastic_PortNum_PRIVATE_APP), concurrency::OSThread("Snake")
{
    inputObserver.observe(inputBroker);
    disable(); // idle until the player launches the game from the menu
}

// ---------------------------------------------------------------------------
// Lifecycle / state transitions
// ---------------------------------------------------------------------------

void SnakeModule::launchGame()
{
    if (!highScoresLoaded)
        loadHighScores();
    // The games frame is already the current frame (the player opened the Games menu from it), so
    // just start playing -- no focus change or frameset regeneration needed.
    startPlaying();
}

void SnakeModule::startPlaying()
{
    game.reset(static_cast<uint32_t>(random()) ^ millis());
    uiState = SNAKE_PLAYING;
    lastAwakeKickMs = millis();
    kickTick();
    requestRedraw(UIFrameEvent::Action::REDRAW_ONLY);
}

void SnakeModule::enterGameOver()
{
    lastScore = game.score();
    if (!highScoresLoaded)
        loadHighScores();
    lastRank = -1;
    lastWasNewTop = false;
    uiState = SNAKE_GAMEOVER;

    // Arcade-style: if the score placed, prompt for initials, then record it in the picker's
    // callback. Otherwise just show the game-over screen.
    if (qualifiesForHighScore(lastScore))
        promptForInitials();

    requestRedraw(UIFrameEvent::Action::REDRAW_ONLY);
}

void SnakeModule::promptForInitials()
{
    screen->showAlphanumericPicker("New High Score!\nEnter initials", "AAA", 60000, INITIALS_LEN,
                                   [this](const std::string &initials) { this->recordHighScore(initials.c_str()); });
}

void SnakeModule::recordHighScore(const char *initials)
{
    bool isNewTop = false;
    lastRank = insertHighScore(lastScore, initials, isNewTop);
    lastWasNewTop = isNewTop;
    if (lastRank >= 0)
        saveHighScores(); // table changed -- the only time we write flash
#if SNAKE_ANNOUNCE_HIGH_SCORE
    if (isNewTop && lastScore > 0)
        announceHighScore(initials, lastScore);
#endif
    requestRedraw(UIFrameEvent::Action::REDRAW_ONLY);
}

void SnakeModule::exitToIdle()
{
    uiState = SNAKE_IDLE;
    disable();
    // The games frame is always present, so we just return it to the attract screen and redraw --
    // no frameset change. interceptingKeyboardInput() now returns false, so the D-pad navigates
    // between frames again.
    requestRedraw(UIFrameEvent::Action::REDRAW_ONLY);
}

void SnakeModule::requestRedraw(UIFrameEvent::Action action)
{
    UIFrameEvent e;
    e.action = action;
    notifyObservers(&e);
}

void SnakeModule::kickTick()
{
    enabled = true;
    setIntervalFromNow(250); // brief beat so the player sees the board before it moves
}

int32_t SnakeModule::tickIntervalMs() const
{
    // Speed ramps up as the snake grows: 160 ms base, down to a 70 ms floor.
    int32_t iv = 160 - static_cast<int32_t>(game.length()) * 3;
    return iv < 70 ? 70 : iv;
}

int32_t SnakeModule::runOnce()
{
    if (uiState != SNAKE_PLAYING)
        return disable();

    if (!game.step()) {
        enterGameOver();
        return disable();
    }

    // Keep the display awake through long straight runs that generate no key presses.
    const uint32_t now = millis();
    if (now - lastAwakeKickMs > 1500) {
        powerFSM.trigger(EVENT_PRESS);
        lastAwakeKickMs = now;
    }

    requestRedraw(UIFrameEvent::Action::REDRAW_ONLY);
    return tickIntervalMs();
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

bool SnakeModule::applyDirection(input_broker_event ev)
{
    switch (ev) {
    case INPUT_BROKER_UP:
        return game.setDirection(SnakeGame::DIR_UP), true;
    case INPUT_BROKER_DOWN:
        return game.setDirection(SnakeGame::DIR_DOWN), true;
    case INPUT_BROKER_LEFT:
        return game.setDirection(SnakeGame::DIR_LEFT), true;
    case INPUT_BROKER_RIGHT:
        return game.setDirection(SnakeGame::DIR_RIGHT), true;
    default:
        return false;
    }
}

int SnakeModule::handleInputEvent(const InputEvent *event)
{
    if (screen && screen->isOverlayBannerShowing())
        return 0; // a menu banner is up; don't steal its input
    // While Tetris owns the screen, all input belongs to it.
    if (tetrisModule && tetrisModule->isActive())
        return 0;

    const input_broker_event ev = event->inputEvent;
    const bool isBack = (ev == INPUT_BROKER_CANCEL || ev == INPUT_BROKER_BACK);

    if (uiState == SNAKE_IDLE) {
        // UP or DOWN on the attract screen shows the Tetris title screen.
        if ((ev == INPUT_BROKER_DOWN || ev == INPUT_BROKER_UP) && tetrisModule && !tetrisModule->isActive()) {
            tetrisModule->showTitle();
            return 1;
        }
        // Long-press SELECT opens the Snake high-score table.
        if (ev == INPUT_BROKER_SELECT_LONG) {
            uiState = SNAKE_HISCORES;
            requestRedraw(UIFrameEvent::Action::REDRAW_ONLY);
            return 1;
        }
        return 0; // all other input passes through for normal frame navigation
    }

    switch (uiState) {
    case SNAKE_PLAYING:
        if (isBack || ev == INPUT_BROKER_SELECT) {
            uiState = SNAKE_PAUSED; // press to pause; from there choose resume or quit
            disable();
            requestRedraw(UIFrameEvent::Action::REDRAW_ONLY);
        } else {
            applyDirection(ev);
        }
        return 1;

    case SNAKE_PAUSED:
        if (isBack) {
            exitToIdle(); // quit from pause
        } else if (ev == INPUT_BROKER_SELECT || ev == INPUT_BROKER_UP || ev == INPUT_BROKER_DOWN || ev == INPUT_BROKER_LEFT ||
                   ev == INPUT_BROKER_RIGHT) {
            uiState = SNAKE_PLAYING;
            applyDirection(ev);
            kickTick();
            requestRedraw(UIFrameEvent::Action::REDRAW_ONLY);
        }
        return 1;

    case SNAKE_GAMEOVER:
        if (ev == INPUT_BROKER_SELECT) {
            uiState = SNAKE_HISCORES;
            requestRedraw(UIFrameEvent::Action::REDRAW_ONLY);
        } else if (isBack) {
            exitToIdle();
        }
        return 1;

    case SNAKE_HISCORES:
        if (ev == INPUT_BROKER_SELECT || isBack)
            exitToIdle();
        return 1;

    default:
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void SnakeModule::drawCenteredLines(OLEDDisplay *display, int16_t x, int16_t y, const char *const *lines, uint8_t count)
{
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    const int16_t lineH = FONT_HEIGHT_SMALL;
    const int16_t total = static_cast<int16_t>(count) * lineH;
    int16_t startY = y + (display->getHeight() - total) / 2;
    if (startY < y)
        startY = y;
    const int16_t cx = x + display->getWidth() / 2;
    for (uint8_t i = 0; i < count; i++)
        display->drawString(cx, startY + i * lineH, lines[i]);
}

void SnakeModule::drawHighScores(OLEDDisplay *display, int16_t x, int16_t y)
{
    display->setFont(FONT_SMALL);
    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(x + display->getWidth() / 2, y, "HIGH SCORES");

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const int16_t rowH = (display->getHeight() - FONT_HEIGHT_SMALL) / HS_COUNT;
    for (uint8_t i = 0; i < HS_COUNT; i++) {
        char row[32];
        if (highScores[i].score > 0) {
            snprintf(row, sizeof(row), "%u. %-4s %lu", static_cast<unsigned>(i + 1), highScores[i].shortName,
                     static_cast<unsigned long>(highScores[i].score));
        } else {
            snprintf(row, sizeof(row), "%u. ---", static_cast<unsigned>(i + 1));
        }
        display->drawString(x + 6, y + FONT_HEIGHT_SMALL + i * rowH, row);
    }
}

void SnakeModule::drawPlayfield(OLEDDisplay *display, int16_t x, int16_t y)
{
    char buf[24];
    display->setColor(WHITE);
    display->setFont(FONT_SMALL);

    // Score bar: current score on the left, best on the right.
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    snprintf(buf, sizeof(buf), "Score %lu", static_cast<unsigned long>(game.score()));
    display->drawString(x + 2, y + 2, buf);
    if (highScores[0].score > 0) {
        display->setTextAlignment(TEXT_ALIGN_RIGHT);
        snprintf(buf, sizeof(buf), "Hi %lu", static_cast<unsigned long>(highScores[0].score));
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

void SnakeModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setColor(WHITE);

    switch (uiState) {
    case SNAKE_IDLE: {
        // Attract screen, shown whenever no game is running on the always-present games frame.
        const int16_t w = display->getWidth();
        const int16_t cx = x + w / 2;
        display->setFont(FONT_SMALL);
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(cx, y, "S N A K E");
        // Snake glyph, centred below the title.
        display->drawXbm(x + (w - snake_width) / 2, y + 15, snake_width, snake_height, snake);
        char hi[32];
        if (highScores[0].score > 0 && highScores[0].shortName[0] != '\0')
            snprintf(hi, sizeof(hi), "High: %s %lu", highScores[0].shortName, static_cast<unsigned long>(highScores[0].score));
        else
            snprintf(hi, sizeof(hi), "High: %lu", static_cast<unsigned long>(highScores[0].score));
        display->drawString(cx, y + 34, hi);
        display->drawString(cx, y + 48, "SELECT to play");
        break;
    }
    case SNAKE_PLAYING:
        drawPlayfield(display, x, y);
        break;
    case SNAKE_PAUSED: {
        drawPlayfield(display, x, y);
        display->setFont(FONT_SMALL);
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + display->getWidth() / 2, y + display->getHeight() / 2 - FONT_HEIGHT_SMALL / 2, "- PAUSED -");
        break;
    }
    case SNAKE_GAMEOVER: {
        char scoreLine[24];
        snprintf(scoreLine, sizeof(scoreLine), "Score: %lu", static_cast<unsigned long>(lastScore));
        const char *status = lastWasNewTop ? "NEW HIGH SCORE!" : lastRank >= 0 ? "You made the top 5!" : "";
        const char *lines[] = {"GAME OVER", scoreLine, status, "SELECT: scores"};
        drawCenteredLines(display, x, y, lines, 4);
        break;
    }
    case SNAKE_HISCORES:
        drawHighScores(display, x, y);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// High-score table
// ---------------------------------------------------------------------------

bool SnakeModule::qualifiesForHighScore(uint32_t score) const
{
    if (score == 0)
        return false;
    for (int i = 0; i < HS_COUNT; i++) {
        if (score > highScores[i].score)
            return true;
    }
    return false;
}

int SnakeModule::insertHighScore(uint32_t score, const char *initials, bool &isNewTop)
{
    isNewTop = false;
    if (score == 0)
        return -1;

    int pos = -1;
    for (int i = 0; i < HS_COUNT; i++) {
        if (score > highScores[i].score) {
            pos = i;
            break;
        }
    }
    if (pos < 0)
        return -1; // not good enough to place

    for (int i = HS_COUNT - 1; i > pos; i--)
        highScores[i] = highScores[i - 1];

    HighScoreEntry &e = highScores[pos];
    memset(&e, 0, sizeof(e));
    e.score = score;
    e.nodeNum = nodeDB ? nodeDB->getNodeNum() : 0;
    strncpy(e.shortName, (initials && initials[0]) ? initials : owner.short_name, sizeof(e.shortName) - 1);
    e.shortName[sizeof(e.shortName) - 1] = '\0';
    e.epoch = getValidTime(RTCQualityDevice, false); // 0 when no valid RTC

    isNewTop = (pos == 0);
    return pos;
}

#if SNAKE_ANNOUNCE_HIGH_SCORE
void SnakeModule::announceHighScore(const char *initials, uint32_t score)
{
    if (!service)
        return;
    meshtastic_MeshPacket *p = allocDataPacket();
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

// ---------------------------------------------------------------------------
// Persistence (SafeFile, atomic; CRC + magic + version guarded)
// ---------------------------------------------------------------------------

void SnakeModule::loadHighScores()
{
    highScoresLoaded = true;
    memset(highScores, 0, sizeof(highScores));
#ifdef FSCom
    concurrency::LockGuard g(spiLock);
    auto f = FSCom.open(SNAKE_HS_FILE, FILE_O_READ);
    if (!f)
        return;
    HighScoreFile file;
    const bool readOk = (f.read(reinterpret_cast<uint8_t *>(&file), sizeof(file)) == sizeof(file));
    f.close();
    if (!readOk || file.magic != HS_MAGIC || file.version != HS_VERSION) {
        LOG_DEBUG("Snake: no valid high-score file, starting fresh");
        return;
    }
    if (crc32Buffer(&file, offsetof(HighScoreFile, crc)) != file.crc) {
        LOG_WARN("Snake: high-score CRC mismatch, resetting table");
        return;
    }
    memcpy(highScores, file.entries, sizeof(highScores));
    LOG_INFO("Snake: loaded high scores (top=%lu)", static_cast<unsigned long>(highScores[0].score));
#endif
}

void SnakeModule::saveHighScores()
{
#ifdef FSCom
    spiLock->lock();
    FSCom.mkdir("/prefs");
    spiLock->unlock();

    HighScoreFile file;
    memset(&file, 0, sizeof(file));
    file.magic = HS_MAGIC;
    file.version = HS_VERSION;
    file.settings = 0; // reserved
    memcpy(file.entries, highScores, sizeof(highScores));
    file.crc = crc32Buffer(&file, offsetof(HighScoreFile, crc));

    auto sf = SafeFile(SNAKE_HS_FILE, true);
    spiLock->lock();
    const size_t written = sf.write(reinterpret_cast<uint8_t *>(&file), sizeof(file));
    // unlock here because SafeFile.close() takes the lock internally
    spiLock->unlock();
    if (!sf.close() || written != sizeof(file))
        LOG_WARN("Snake: failed to save high scores");
#endif
}

#endif // HAS_SCREEN && BASEUI_HAS_GAMES
