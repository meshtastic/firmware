#include "GamesModule.h"

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "Breakout.h"
#include "ChirpyRunner.h"
#include "PowerFSM.h"
#include "Snake.h"
#include "Tetris.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "main.h"
#include "mesh/NodeDB.h"
#if GAMES_ANNOUNCE_HIGH_SCORE
#include "MeshService.h"
#endif

GamesModule *gamesModule;

GamesModule::GamesModule() : SinglePortModule("games", meshtastic_PortNum_PRIVATE_APP), concurrency::OSThread("Games")
{
    // Register the hosted games. Order sets the attract-screen cycle order (Snake is shown first).
    games.push_back(new Snake());
    games.push_back(new Tetris());
    games.push_back(new ChirpyRunner());
    games.push_back(new Breakout());
    inputObserver.observe(inputBroker);

    // Keep the tick thread alive at boot only if a game broadcasts periodically; otherwise idle
    // until the player launches a game. The first idle tick reschedules to the real cadence.
    bool periodic = false;
    for (Game *g : games)
        periodic = periodic || g->wantsPeriodicMesh();
    if (periodic)
        setIntervalFromNow(1000);
    else
        disable();
}

// ---------------------------------------------------------------------------
// Lifecycle / state transitions
// ---------------------------------------------------------------------------

void GamesModule::launchGame()
{
    if (games.empty())
        return;
    // The games frame is already current (the player is on the attract screen), so just begin play
    // with the selected game -- no focus change or frameset regeneration needed.
    active = games[selected];
    startPlaying();
}

void GamesModule::startPlaying()
{
    active->start(static_cast<uint32_t>(random()) ^ millis());
    uiState = GAMES_PLAYING;
    lastAwakeKickMs = millis();
    kickTick();
    requestRedraw();
}

void GamesModule::enterGameOver()
{
    lastScore = active ? active->score() : 0;
    lastRank = -1;
    lastWasNewTop = false;
    uiState = GAMES_GAMEOVER;

    // Arcade-style: if the score placed, prompt for initials, then record it in the picker's
    // callback. Otherwise just show the game-over screen.
    if (active && active->scores().qualifies(lastScore))
        promptForInitials();

    requestRedraw();
}

void GamesModule::promptForInitials()
{
    screen->showAlphanumericPicker("New High Score!\nEnter initials", "AAA", 60000, HighScoreTableBase::INITIALS_LEN,
                                   [this](const std::string &initials) { this->recordHighScore(initials.c_str()); });
}

void GamesModule::recordHighScore(const char *initials)
{
    if (!active)
        return;
    bool isNewTop = false;
    lastRank = active->scores().insert(lastScore, initials, nodeDB ? nodeDB->getNodeNum() : 0, isNewTop);
    lastWasNewTop = isNewTop;
    if (lastRank >= 0)
        active->scores().save(); // table changed -- the only time we write flash
#if GAMES_ANNOUNCE_HIGH_SCORE
    if (isNewTop && lastScore > 0)
        announceHighScore(initials, lastScore);
#endif
    requestRedraw();
}

#if GAMES_ANNOUNCE_HIGH_SCORE
void GamesModule::announceHighScore(const char *initials, uint32_t score)
{
    if (!active || !service)
        return;
    if (!initials || initials[0] == '\0')
        return;
    meshtastic_MeshPacket *p = allocDataPacket();
    p->to = NODENUM_BROADCAST;
    p->channel = 0; // primary channel
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    // One shared message for every game, with the game's name spliced in. ASCII only -- avoids tofu
    // if a receiving node's font lacks a glyph.
    p->decoded.payload.size = snprintf(reinterpret_cast<char *>(p->decoded.payload.bytes), sizeof(p->decoded.payload.bytes),
                                       GAMES_HIGH_SCORE_STRING, active->name(), initials, static_cast<unsigned long>(score));
    service->sendToMesh(p);
    LOG_INFO("Games: announced new %s high score %lu", active->name(), static_cast<unsigned long>(score));
}
#endif

void GamesModule::exitToIdle()
{
    uiState = GAMES_IDLE;
    active = nullptr;
    // The games frame is always present, so we just return it to the attract screen and redraw --
    // no frameset change. interceptingKeyboardInput() now returns false, so the D-pad navigates
    // between frames again. Keep ticking only if a game still needs its periodic broadcast.
    bool periodic = false;
    for (Game *g : games)
        periodic = periodic || g->wantsPeriodicMesh();
    if (periodic)
        setIntervalFromNow(1000);
    else
        disable();
    requestRedraw();
}

void GamesModule::requestRedraw()
{
    UIFrameEvent e;
    e.action = UIFrameEvent::Action::REDRAW_ONLY;
    notifyObservers(&e);
}

void GamesModule::kickTick()
{
    enabled = true;
    setIntervalFromNow(250); // brief beat so the player sees the board before it moves
}

int32_t GamesModule::runOnce()
{
    if (uiState == GAMES_PLAYING && active) {
        if (!active->tick()) {
            enterGameOver();
            return disable();
        }

        // Keep the display awake through long runs that generate no key presses.
        const uint32_t now = millis();
        if (now - lastAwakeKickMs > 1500) {
            powerFSM.trigger(EVENT_PRESS);
            lastAwakeKickMs = now;
        }

        requestRedraw();
        return active->tickIntervalMs();
    }

    // Idle: service any game that broadcasts periodically; sleep until the soonest one is due.
    int32_t next = -1;
    for (Game *g : games) {
        const int32_t due = g->meshTick(*this);
        if (due >= 0 && (next < 0 || due < next))
            next = due;
    }
    return next < 0 ? disable() : next;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

int GamesModule::handleInputEvent(const InputEvent *event)
{
    if (screen && screen->isOverlayBannerShowing())
        return 0; // a menu banner is up; don't steal its input

    const input_broker_event ev = event->inputEvent;
    const bool isBack = (ev == INPUT_BROKER_CANCEL || ev == INPUT_BROKER_BACK);

    switch (uiState) {
    case GAMES_IDLE:
        // Attract screen: UP/DOWN cycle which game is shown; SELECT (handled by Screen) launches it;
        // long-press SELECT opens that game's high-score table. Everything else passes through so
        // the D-pad still navigates between frames.
        if (!games.empty() && (ev == INPUT_BROKER_DOWN || ev == INPUT_BROKER_UP)) {
            const uint8_t n = static_cast<uint8_t>(games.size());
            selected = (ev == INPUT_BROKER_DOWN) ? (selected + 1) % n : (selected + n - 1) % n;
            requestRedraw();
            return 1;
        }
        if (ev == INPUT_BROKER_SELECT_LONG && !games.empty()) {
            active = games[selected];
            uiState = GAMES_HISCORES;
            requestRedraw();
            return 1;
        }
        return 0;

    case GAMES_PLAYING:
        if (isBack) {
            uiState = GAMES_PAUSED; // BACK to pause; from there choose resume or quit
            disable();
            requestRedraw();
        } else if (active) {
            active->handleInput(ev);
            if (!active->isPlaying()) {
                enterGameOver();
                return 1;
            }
            requestRedraw();
        }
        return 1;

    case GAMES_PAUSED:
        if (isBack) {
            exitToIdle(); // quit from pause
        } else if (ev == INPUT_BROKER_SELECT || ev == INPUT_BROKER_UP || ev == INPUT_BROKER_DOWN || ev == INPUT_BROKER_LEFT ||
                   ev == INPUT_BROKER_RIGHT) {
            uiState = GAMES_PLAYING;
            kickTick();
            requestRedraw();
        }
        return 1;

    case GAMES_GAMEOVER:
        if (ev == INPUT_BROKER_SELECT) {
            uiState = GAMES_HISCORES;
            requestRedraw();
        } else if (isBack) {
            exitToIdle();
        }
        return 1;

    case GAMES_HISCORES:
        if (ev == INPUT_BROKER_SELECT_LONG && active) {
            static const char *opts[] = {"No", "Yes"};
            graphics::BannerOverlayOptions confirm;
            confirm.message = "Clear Scores?";
            confirm.optionsArrayPtr = opts;
            confirm.optionsCount = 2;
            confirm.bannerCallback = [this](int sel) {
                if (sel == 1 && active) {
                    active->scores().clear();
                    active->scores().save();
                    requestRedraw();
                    LOG_INFO("Games: high scores cleared");
                }
            };
            if (screen)
                screen->showOverlayBanner(confirm);
        } else if (ev == INPUT_BROKER_SELECT || isBack) {
            exitToIdle();
        }
        return 1;

    default:
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void GamesModule::drawCenteredLines(OLEDDisplay *display, int16_t x, int16_t y, const char *const *lines, uint8_t count)
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

void GamesModule::drawHighScores(OLEDDisplay *display, int16_t x, int16_t y, HighScoreTableBase &scores)
{
    display->setFont(FONT_SMALL);
    display->setColor(WHITE);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(x, y, "HIGH SCORES");
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    const int16_t rowH = (display->getHeight() - FONT_HEIGHT_SMALL) / HighScoreTableBase::HS_COUNT;
    for (uint8_t i = 0; i < HighScoreTableBase::HS_COUNT; i++) {
        char row[32];
        if (scores.scoreAt(i) > 0) {
            snprintf(row, sizeof(row), "%u. %-4s %lu", static_cast<unsigned>(i + 1), scores.nameAt(i),
                     static_cast<unsigned long>(scores.scoreAt(i)));
        } else {
            snprintf(row, sizeof(row), "%u. ---", static_cast<unsigned>(i + 1));
        }
        display->drawString(x + 6, y + FONT_HEIGHT_SMALL + i * rowH, row);
    }
}

void GamesModule::drawFrame(OLEDDisplay *display, OLEDDisplayUiState * /*state*/, int16_t x, int16_t y)
{
    display->setColor(WHITE);

    switch (uiState) {
    case GAMES_IDLE:
        if (!games.empty())
            games[selected]->drawAttract(display, x, y);
        break;

    case GAMES_PLAYING:
        if (active)
            active->drawPlaying(display, x, y);
        break;

    case GAMES_PAUSED:
        if (active) {
            active->drawPlaying(display, x, y);
            display->setFont(FONT_SMALL);
            display->setTextAlignment(TEXT_ALIGN_CENTER);
            display->drawString(x + display->getWidth() / 2, y + display->getHeight() / 2 - FONT_HEIGHT_SMALL / 2, "- PAUSED -");
        }
        break;

    case GAMES_GAMEOVER: {
        char scoreLine[24];
        snprintf(scoreLine, sizeof(scoreLine), "Score: %lu", static_cast<unsigned long>(lastScore));
        const char *status = lastWasNewTop ? "NEW HIGH SCORE!" : (lastRank >= 0 ? "You made the top 5!" : "");
        const char *hint = active ? active->gameOverHint() : "SELECT: scores";
        const char *lines[] = {"GAME OVER", scoreLine, status, hint};
        drawCenteredLines(display, x, y, lines, 4);
        break;
    }

    case GAMES_HISCORES:
        if (active)
            drawHighScores(display, x, y, active->scores());
        break;

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Mesh receive - dispatch to each hosted game
// ---------------------------------------------------------------------------

ProcessMessage GamesModule::handleReceived(const meshtastic_MeshPacket &mp)
{
    for (Game *g : games) {
        const ProcessMessage r = g->handleReceived(mp);
        if (r != ProcessMessage::CONTINUE)
            return r;
    }
    requestRedraw(); // a merged remote score should show up if the high-score screen is open
    return ProcessMessage::CONTINUE;
}

#endif // HAS_SCREEN && BASEUI_HAS_GAMES
