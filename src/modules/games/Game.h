#pragma once

#include "configuration.h"

#if HAS_SCREEN && BASEUI_HAS_GAMES

#include "HighScoreTable.h"
#include "input/InputBroker.h" // input_broker_event
#include "mesh/MeshModule.h"   // ProcessMessage, meshtastic_MeshPacket
#include <cstdint>

class OLEDDisplay;
class OLEDDisplayUiState;
class GamesModule;

/**
 * A single game hosted by GamesModule. The host owns the shared UI state machine (attract /
 * playing / paused / game-over / high-scores), the initials picker, high-score persistence calls,
 * the tick thread, and the mesh port; each Game supplies only the game-specific pieces: its
 * attract art, its playfield, its per-key input while playing, its speed curve, and its own
 * high-score table (and, optionally, a mesh announce/receive protocol).
 */
class Game
{
  public:
    virtual ~Game() = default;

    virtual const char *name() const = 0;

    // --- Lifecycle ---
    virtual void start(uint32_t seed) = 0; // (re)start the underlying game logic
    virtual bool tick() = 0;               // advance one step; returns isPlaying() afterwards
    virtual bool isPlaying() const = 0;
    virtual uint32_t score() const = 0;
    virtual int32_t tickIntervalMs() const = 0; // per-game speed curve

    // --- Input while PLAYING (the host handles the BACK-to-pause and menu keys) ---
    virtual void handleInput(input_broker_event ev) = 0;

    // --- Rendering (the host draws the shared PAUSED / GAME OVER / HIGH SCORES chrome) ---
    virtual void drawAttract(OLEDDisplay *display, int16_t x, int16_t y) = 0; // title/art + hi + hint
    virtual void drawPlaying(OLEDDisplay *display, int16_t x, int16_t y) = 0; // playfield only
    virtual const char *gameOverHint() const { return "SELECT: scores"; }

    // --- High scores (the host runs the initials picker + save) ---
    virtual HighScoreTableBase &scores() = 0;

    // --- Mesh (defaults are no-ops; only games with a wire protocol override these) ---
    // Note: the new-high-score announcement is NOT here -- it is shared by every game and lives in
    // GamesModule (which splices name() into one common message).
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) { return ProcessMessage::CONTINUE; }
    virtual bool wantsPeriodicMesh() const { return false; }
    // Perform any due periodic broadcast and return ms until the next one (-1 == nothing pending).
    virtual int32_t meshTick(GamesModule &host) { return -1; }
};

#endif // HAS_SCREEN && BASEUI_HAS_GAMES
