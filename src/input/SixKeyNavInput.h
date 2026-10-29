#pragma once

#if defined(EINK_RA01S_GP02)

#include "InputBroker.h"
#include "concurrency/OSThread.h"

/**
 * SixKeyNavInput - 6-button navigation driver for eink-ra01s-gp02.
 *
 * Buttons (all ACTIVE_LOW with hardware pull-up):
 *   Up, Down, Left, Right, Enter, Fn
 *
 * Events (all fired on key RELEASE, after per-key debounce):
 *   Up/Down/Left/Right/Enter (Fn NOT held) → INPUT_BROKER_* (via InputBroker)
 *   Fn + Up/Down/Left/Right/Enter → onCombo* callback
 *   Fn held ≥ 4 s → onFnHeld (shutdown), fires once
 *   Fn short press → no-op (Fn is modifier-only)
 *
 * Interrupt-driven: all 6 pins use the shared GPIOTE PORT event (GPIO
 * SENSE/DETECT) - zero dedicated GPIOTE channels, ~2.4 µA System ON sleep.
 * DO NOT use attachInterrupt(): it forces a dedicated channel (~17 µA each).
 * The ISR only latches each key's level (pressed) and a sticky release flag;
 * the OSThread does all timing (per-key debounce with millis(), Fn long-press)
 * in thread context. The single-bit release latch collapses switch bounce, so
 * at most one event fires per physical release (no double-fire).
 */
class SixKeyNavInput : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    const char *_originName;

    SixKeyNavInput();
    bool init();
    int32_t runOnce() override;

    // Combo-key callbacks: assign externally to inject behavior.
    // Left as nullptr → combo is silently ignored.
    void (*onComboUp)();
    void (*onComboDown)();
    void (*onComboLeft)();
    void (*onComboRight)();
    void (*onComboEnter)();
    void (*onFnHeld)();

    void fireEvent(input_broker_event evt);
    void fireKbEvent(uint8_t kbchar); // kbchar-routed system commands

    // Called from the file-local ISR (gpiotePortHandler) to sample all pin
    // levels immediately and store them as a volatile snapshot for runOnce().
    void captureSnapshotFromISR();

  private:
    static const uint32_t SHUTDOWN_MS = 4000; // Fn hold threshold for shutdown
    static const uint32_t DEBOUNCE_MS = 20;   // min ms between dispatched events

    struct KeyState {
        uint8_t pin;           // GPIO pin (set in init)
        volatile bool pressed; // current level (ISR-write, thread-read)
        volatile bool release; // release edge pending (ISR-set, thread-clear)
        uint32_t lastChangeMs; // last dispatch time (thread, debounce)
    };

    // Navigation buttons (indices 0-4: Up, Down, Left, Right, Enter)
    KeyState _nav[5];

    // Fn button - modifier key: triggers combos (hold+nav) or shutdown (hold≥4s)
    KeyState _fn;
    bool _fnConsumed;      // true: Fn already produced a combo/shutdown
    uint32_t _fnPressTime; // Fn press start (long-press timer)
};

extern SixKeyNavInput *sixKeyNavInput;

#endif // EINK_RA01S_GP02
