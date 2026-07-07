#pragma once

#include "configuration.h"

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "Observer.h"
#include "SnakeGame.h"
#include "concurrency/OSThread.h"
#include "input/InputBroker.h"
#include "mesh/SinglePortModule.h"

// High-score mesh announcement is a COMPILE-TIME option, default OFF. Broadcasting to shared
// airtime must be opted into at build time with -DSNAKE_ANNOUNCE_HIGH_SCORE=1; when disabled
// (the default) the announcement code is compiled out entirely -- there is no runtime toggle.
#ifndef SNAKE_ANNOUNCE_HIGH_SCORE
#define SNAKE_ANNOUNCE_HIGH_SCORE 0
#endif

enum SnakeUiState : uint8_t {
    SNAKE_IDLE,     // attract screen on the always-present games frame; OSThread disabled
    SNAKE_PLAYING,  // game running; OSThread ticking
    SNAKE_PAUSED,   // paused mid-game; tick disabled
    SNAKE_GAMEOVER, // final score / new-high notice
    SNAKE_HISCORES, // top-5 table
};

/**
 * Snake, as a Meshtastic BaseUI games frame. Rather than a generic module frame, it is drawn on a
 * dedicated, always-present frame placed right after home (see Screen::setFrames); SELECT on that
 * frame starts a game directly. Owns a pure SnakeGame (game logic), a small UI state machine, an
 * OSThread game tick, a flash-persisted top-5 high-score table, and an optional (compile-time,
 * default-off) mesh announcement when a new all-time #1 is set.
 *
 * Sits on PRIVATE_APP but does not receive packets in v1 -- the port is reserved for a future
 * head-to-head mode. The high-score announcement is a plain TEXT_MESSAGE_APP broadcast.
 */
class SnakeModule : public SinglePortModule, public Observable<const UIFrameEvent *>, private concurrency::OSThread
{
  public:
    SnakeModule();

    /// Start a game (invoked when SELECT is pressed on the games frame). The games frame is
    /// already current, so this just begins play.
    void launchGame();

    // Drawn through the dedicated games-frame trampoline, and queried by Screen's input gating, so
    // these MeshModule overrides are public. While a game is active we own the D-pad (turns/pause);
    // when idle the attract screen shows and the D-pad navigates between frames as usual.
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) override;
    virtual bool interceptingKeyboardInput() override { return uiState != SNAKE_IDLE; }

  protected:
    virtual int32_t runOnce() override; // game tick
    virtual Observable<const UIFrameEvent *> *getUIFrameObservable() override { return this; }

  private:
    int handleInputEvent(const InputEvent *event);
    CallbackObserver<SnakeModule, const InputEvent *> inputObserver =
        CallbackObserver<SnakeModule, const InputEvent *>(this, &SnakeModule::handleInputEvent);

    // === State transitions ===
    void startPlaying();
    void enterGameOver();
    void exitToIdle();
    void requestRedraw(UIFrameEvent::Action action);
    void kickTick();
    int32_t tickIntervalMs() const;
    bool applyDirection(input_broker_event ev); // returns true if ev was a handled direction

    // === Rendering ===
    void drawPlayfield(OLEDDisplay *display, int16_t x, int16_t y);
    void drawCenteredLines(OLEDDisplay *display, int16_t x, int16_t y, const char *const *lines, uint8_t count);
    void drawHighScores(OLEDDisplay *display, int16_t x, int16_t y);

    // === High-score persistence ===
    struct HighScoreEntry {
        uint32_t score;
        uint32_t nodeNum;
        char shortName[5]; // three characters, NUL-terminated
        uint32_t epoch;    // getValidTime(), 0 if no RTC
    } __attribute__((packed));

    static constexpr uint8_t HS_COUNT = 5;
    static constexpr uint32_t HS_MAGIC = 0x534E454Bu; // 'SNEK'
    static constexpr uint8_t HS_VERSION = 1;
    static constexpr uint8_t INITIALS_LEN = 3; // arcade-style initials captured per high score

    struct HighScoreFile {
        uint32_t magic;
        uint8_t version;
        uint8_t settings; // reserved for future flags
        uint16_t reserved;
        HighScoreEntry entries[HS_COUNT];
        uint32_t crc; // crc32 over every preceding byte
    } __attribute__((packed));

    void loadHighScores();
    void saveHighScores();
    // True if `score` would place on the sorted-descending table (peek; no mutation).
    bool qualifiesForHighScore(uint32_t score) const;
    // Insert into the sorted-descending table under the given initials. Returns the 0-based rank
    // if it placed, else -1. isNewTop is set when the score took the #1 slot.
    int insertHighScore(uint32_t score, const char *initials, bool &isNewTop);
    // Arcade-style flow: open the initials picker (or fall back to the node short name when
    // headless), then record + persist the score in the picker's callback.
    void promptForInitials();
    void recordHighScore(const char *initials);
#if SNAKE_ANNOUNCE_HIGH_SCORE
    void announceHighScore(const char *initials, uint32_t score);
#endif

    HighScoreEntry highScores[HS_COUNT] = {};
    bool highScoresLoaded = false;

    // === Game state ===
    SnakeGame game;
    SnakeUiState uiState = SNAKE_IDLE;
    uint32_t lastScore = 0;       // score of the just-finished game (for the GAME_OVER screen)
    int lastRank = -1;            // rank achieved last game (-1 == didn't place)
    bool lastWasNewTop = false;   // last game set a new all-time #1
    uint32_t lastAwakeKickMs = 0; // throttles the power-FSM wake nudge during long runs
};

extern SnakeModule *snakeModule;

#endif // HAS_SCREEN && BASEUI_HAS_GAMES
