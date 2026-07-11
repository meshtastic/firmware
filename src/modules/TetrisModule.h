#pragma once

#include "configuration.h"

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "Observer.h"
#include "TetrisGame.h"
#include "concurrency/OSThread.h"
#include "input/InputBroker.h"
#include "mesh/SinglePortModule.h"

// High-score mesh announcement is a COMPILE-TIME option, default OFF.
#ifndef TETRIS_ANNOUNCE_HIGH_SCORE
#define TETRIS_ANNOUNCE_HIGH_SCORE 0
#endif

enum TetrisUiState : uint8_t {
    TETRIS_INACTIVE, // not launched; games frame shows the Snake attract screen
    TETRIS_TITLE,    // title/attract screen for Tetris
    TETRIS_PLAYING,
    TETRIS_PAUSED,
    TETRIS_GAMEOVER,
    TETRIS_HISCORES,
};

/**
 * Tetris, as a second game on the Meshtastic BaseUI games frame.
 *
 * Launched when the player presses DOWN on the Snake attract screen.
 * Uses the 128×64 OLED in portrait orientation: the 128-pixel axis is the
 * board's vertical direction (top→left edge, bottom→right edge) and the
 * 64-pixel axis is the board's horizontal direction.  This gives a 20×10-cell
 * Tetris board at 4 px/cell occupying the left 80 px of the physical screen,
 * with a score/next-piece panel in the remaining 48 px.
 *
 * Controls while playing:
 *   UP    - rotate CW       LEFT  - move left
 *   RIGHT - move right      DOWN  - soft drop
 *   SELECT - hard drop      BACK  - pause
 *
 * Optional mesh broadcast of high scores gated by -DTETRIS_ANNOUNCE_HIGH_SCORE=1.
 * Wire packets carry a game_id byte ('T') so Snake and Tetris packets are never
 * confused (Tetris wire sizes are 11 / 68 bytes; Snake's are 10 / 67).
 */
class TetrisModule : public SinglePortModule, public Observable<const UIFrameEvent *>, private concurrency::OSThread
{
  public:
    TetrisModule();

    /** Show the Tetris title screen (called when DOWN is pressed on the Snake attract screen). */
    void showTitle();

    /** Start a game immediately (called from the title screen on SELECT). */
    void launchGame();

    /** Returns true while Tetris owns the screen (title/playing/paused/gameover/scores). */
    bool isActive() const { return uiState != TETRIS_INACTIVE; }

    /** True only while the title/attract screen is showing (nav bar stays visible). */
    bool isTitleScreen() const { return uiState == TETRIS_TITLE; }

    /** Drawn through the shared games-frame trampoline when isActive(). */
    void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);

    /** Input is intercepted while playing/paused/gameover/hiscores.
     *  The title screen does NOT intercept so the nav bar remains visible. */
    virtual bool interceptingKeyboardInput() override { return isActive() && uiState != TETRIS_TITLE; }

  protected:
    virtual int32_t runOnce() override; // game-tick thread
    virtual Observable<const UIFrameEvent *> *getUIFrameObservable() override { return this; }
    virtual bool wantUIFrame() override { return false; } // shares the games frame; no own slot
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  private:
    int handleInputEvent(const InputEvent *event);
    CallbackObserver<TetrisModule, const InputEvent *> inputObserver =
        CallbackObserver<TetrisModule, const InputEvent *>(this, &TetrisModule::handleInputEvent);

    // === State transitions ===
    void startPlaying();
    void enterGameOver();
    void exitToIdle();
    void requestRedraw();
    void kickTick();
    int32_t tickIntervalMs() const;

    // === Rendering ===
    void drawPlayfield(OLEDDisplay *display, int16_t x, int16_t y);
    void drawCenteredLines(OLEDDisplay *display, int16_t x, int16_t y, const char *const *lines, uint8_t count);
    void drawHighScores(OLEDDisplay *display, int16_t x, int16_t y);

    // === High-score persistence (local-only, no mesh broadcast) ===
    struct HighScoreEntry {
        uint32_t score;
        char shortName[5]; // NUL-terminated 3-char display name
        uint32_t nodeNum;
        uint32_t epoch;
    } __attribute__((packed));

    static constexpr uint8_t HS_COUNT = 5;
    static constexpr uint32_t HS_MAGIC = 0x54455452u; // 'TETR'
    static constexpr uint8_t HS_VERSION = 1;
    static constexpr uint8_t INITIALS_LEN = 3; // arcade-style initials captured per high score

    struct HighScoreFile {
        uint32_t magic;
        uint8_t version;
        uint8_t reserved[3];
        HighScoreEntry entries[HS_COUNT];
        uint32_t crc;
    } __attribute__((packed));

    void loadHighScores();
    void saveHighScores();
    bool qualifiesForHighScore(uint32_t score) const;
    int insertHighScore(uint32_t score, const char *name, uint32_t nodeNum, bool &isNewTop);
    // Arcade-style flow: open the initials picker (or fall back to the node short name when
    // headless), then record + persist the score in the picker's callback.
    void promptForInitials();
    void recordHighScore(const char *initials);
#if TETRIS_ANNOUNCE_HIGH_SCORE
    void announceHighScore(const char *initials, uint32_t score);
    void broadcastAllScores();
    int32_t nextBroadcastIntervalMs() const;
#endif

    HighScoreEntry highScores[HS_COUNT] = {};
    bool highScoresLoaded = false;

    // === Game state ===
    TetrisGame game;
    TetrisUiState uiState = TETRIS_INACTIVE;
    uint32_t lastScore = 0;
    int lastRank = -1;
    bool lastWasNewTop = false;
    uint32_t lastAwakeKickMs = 0;
#if TETRIS_ANNOUNCE_HIGH_SCORE
    uint32_t lastBroadcastMs = 0;
#endif
};

extern TetrisModule *tetrisModule;

#endif // HAS_SCREEN && BASEUI_HAS_GAMES
