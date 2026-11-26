#pragma once

#include "Throttle.h"
#include "UptimeClock.h"
#include <cstdint>

// Repeats an action while a key is held: a held Backspace deletes a word at a time. Keyboards that report releases (or a
// long press) feed it press and release; the keyboard thread asks whether a repeat is due and when the next one is.
class HeldKeyRepeat
{
  public:
    static constexpr uint32_t REPEAT_MS = 500;

    void press()
    {
#ifndef MESHTASTIC_INCLUDE_INKHUD // InkHUD has no delete-word; its keys keep their own handling
        held = true;
        repeated = false;
        lastMs = Time::getMillis();
#endif
    }

    // Another key ends the hold, in case the held key's release was lost
    void cancel() { held = repeated = false; }

    // Ends the hold; true if it repeated, so a keyboard that acts on release must not also act on this one
    bool release()
    {
        const bool r = held && repeated;
        held = repeated = false;
        return r;
    }

    // True once per REPEAT_MS while held, starting REPEAT_MS after the press
    bool due()
    {
        if (!held || !Throttle::hasElapsed(lastMs, REPEAT_MS))
            return false;
        lastMs = Time::getMillis();
        repeated = true;
        return true;
    }

    // ms until the next repeat: 0 if now, UINT32_MAX while nothing is held
    uint32_t dueInMs() const { return held ? Throttle::remainingMs(lastMs, REPEAT_MS) : UINT32_MAX; }

  private:
    bool held = false;
    bool repeated = false;
    uint32_t lastMs = 0;
};
