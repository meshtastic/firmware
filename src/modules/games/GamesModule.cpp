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
    noteActivity();
    kickTick();
    requestRedraw();
}

void GamesModule::goHome()
{
    // Left idle too long: drop any game and return to the clearly-Meshtastic home frame.
    exitToIdle();
    if (screen)
        screen->showHomeFrame();
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
    if (!p)
        return;
    p->to = NODENUM_BROADCAST;
    p->channel = 0; // primary channel
    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    // One shared message for every game, with the game's name spliced in. ASCII only -- avoids tofu
    // if a receiving node's font lacks a glyph.
    p->decoded.payload.size = snprintf(reinterpret_cast<char *>(p->decoded.payload.bytes), sizeof(p->decoded.payload.bytes),
                                       GAMES_HIGH_SCORE_STRING, active->name(), static_cast<unsigned long>(score), initials);
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
    const uint32_t now = millis();

    // Whether the games UI is actually in front of the player: a game is active (which forces the
    // games frame), or the attract screen is the current frame. When it is, an idle stretch bounces
    // back to the home frame so a walked-away device reads as a Meshtastic node.
    const bool gamesVisible = (uiState != GAMES_IDLE) || (screen && screen->isGamesFrameShown());
    if (gamesVisible) {
        if (screen && screen->isOverlayBannerShowing()) {
            // A picker or banner is up (e.g. high-score initials entry, which our handleInputEvent
            // never sees). The user is busy with it -- don't time out and yank them away.
            lastActivityMs = now;
        } else if (now - lastActivityMs >= INACTIVITY_TIMEOUT_MS) {
            goHome();
            return disable();
        }
    } else {
        // Not in front of the player (attract screen is just one of the rotating frames, and the
        // player is elsewhere): keep the timer fresh so a later visit starts a full 15 s.
        lastActivityMs = now;
    }

    if (uiState == GAMES_PLAYING && active) {
        if (!active->tick()) {
            enterGameOver();
            return disable();
        }

        // Keep the display awake through long runs that generate no key presses.
        if (now - lastAwakeKickMs > 1500) {
            powerFSM.trigger(EVENT_PRESS);
            lastAwakeKickMs = now;
        }

        requestRedraw();
        return active->tickIntervalMs();
    }

    // Idle-ish (attract / paused / game-over / high scores): service any periodic mesh broadcast,
    // and while the games UI is visible keep a slow poll running so the inactivity timeout fires.
    int32_t next = -1;
    for (Game *g : games) {
        const int32_t due = g->meshTick(*this);
        if (due >= 0 && (next < 0 || due < next))
            next = due;
    }
    if (gamesVisible) {
        const int32_t poll = 1000;
        return (next >= 0 && next < poll) ? next : poll;
    }
    return next < 0 ? disable() : next;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

int GamesModule::handleInputEvent(const InputEvent *event)
{
    // Ignore all input unless the games frame is the one actually on screen -- otherwise the attract
    // screen's UP/DOWN would hijack normal frame navigation from wherever the player happens to be.
    if (!screen || !screen->isGamesFrameShown())
        return 0;
    if (screen->isOverlayBannerShowing())
        return 0; // a menu banner is up; don't steal its input

    noteActivity(); // any input on the games frame resets the return-to-home timer

    const input_broker_event ev = event->inputEvent;
    const bool isBack = (ev == INPUT_BROKER_CANCEL || ev == INPUT_BROKER_BACK);

    // Start is mapped to select like any other button, so it launches games and works the menus.
    // Inside a running game it means pause/resume instead, which we can tell only because kbchar
    // names the physical button behind the action.
    const bool isPauseButton = (ev == INPUT_BROKER_SELECT && isJoyStartButton(event->kbchar));

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
        // Start pauses, and is never forwarded to the game: unlike BACK it has no second meaning
        // in play, so a game cannot claim it the way Breakout claims BACK to serve.
        if (isPauseButton) {
            uiState = GAMES_PAUSED;
            disable();
            requestRedraw();
            return 1;
        }
        // BACK pauses, unless the active game has temporarily claimed that button (see
        // Game::wantsBackButton) -- then it is forwarded like any other key.
        if (isBack && !(active && active->wantsBackButton())) {
            uiState = GAMES_PAUSED; // BACK to pause; from there choose resume or quit
            disable();
            requestRedraw();
        } else if (active) {
            active->handleInput(ev, event->kbchar);
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
                   ev == INPUT_BROKER_RIGHT) { // Start arrives as SELECT, so it resumes too
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

    // drawFrame runs only while the games frame is the current frame. Several idle-ish states
    // (attract / paused / game-over / high-scores) otherwise leave the tick thread asleep, so use
    // this render as the trigger to (re)start the slow inactivity poll and reset the timer -- so
    // arriving on such a screen gets a fresh 15 s before we bounce back home, and walking away from
    // it eventually does. (While a game is PLAYING the thread is already ticking, so !enabled is
    // false here and the play-time timer is left to run.)
    if (!enabled) {
        noteActivity();
        enabled = true;
        setIntervalFromNow(1000);
    }

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
