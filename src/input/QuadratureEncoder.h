#pragma once

#include "InputBroker.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <atomic>

// -----------------------------------------------------------------------------
// Generic incremental (quadrature) rotary encoder wired straight to two GPIOs,
// with an optional push switch on a third.
//
// A variant opts in with INPUTDRIVER_ENCODER_TYPE 4 plus the two channel pins.
// Everything else has a default below, so the common case is four lines in
// variant.h. There is no runtime enable and no moduleConfig coupling: the pins
// are a hardware fact, so they live with the board.
//
// | macro                                 | default              |
// | ------------------------------------- | -------------------- |
// | INPUTDRIVER_ENCODER_TYPE              | (4 selects this)     |
// | INPUTDRIVER_ENCODER_A / _B            | (required)           |
// | INPUTDRIVER_ENCODER_BTN               | (optional switch)    |
// | INPUTDRIVER_ENCODER_ACTIVE_LOW        | 1                    |
// | INPUTDRIVER_ENCODER_PULLUP            | 1                    |
// | INPUTDRIVER_ENCODER_STEPS_PER_DETENT  | 4                    |
// | INPUTDRIVER_ENCODER_INVERT            | 0                    |
// | INPUTDRIVER_ENCODER_EVENT_CW / _CCW   | DOWN / UP            |
// | INPUTDRIVER_ENCODER_EVENT_PRESS       | SELECT               |
// | INPUTDRIVER_ENCODER_EVENT_PRESS_LONG  | SELECT_LONG          |
// | INPUTDRIVER_ENCODER_LONG_PRESS_MS     | 300                  |
// | INPUTDRIVER_ENCODER_BTN_DEBOUNCE_MS   | 25                   |
//
// Naming note: INPUTDRIVER_ENCODER_BTN is deliberately the existing macro from
// that family rather than a new one, because src/sleep.cpp already arms it as a
// light-sleep wake source. Reusing it means this driver needs no sleep.cpp edit.
// -----------------------------------------------------------------------------

// Commons tied to GND with pull-ups on the signals - the usual wiring for a bare
// mechanical encoder (ALPS EC11 and friends).
#ifndef INPUTDRIVER_ENCODER_ACTIVE_LOW
#define INPUTDRIVER_ENCODER_ACTIVE_LOW 1
#endif

// The push switch is a separate contact from the two channels and is not always wired the same
// way round, so it gets its own polarity. Defaults to the channels' so a board that shares one
// convention only states it once.
#ifndef INPUTDRIVER_ENCODER_BTN_ACTIVE_LOW
#define INPUTDRIVER_ENCODER_BTN_ACTIVE_LOW INPUTDRIVER_ENCODER_ACTIVE_LOW
#endif

// Internal pulls, matched to each input's polarity. Harmless when the module has its own.
#ifndef INPUTDRIVER_ENCODER_PULLUP
#define INPUTDRIVER_ENCODER_PULLUP 1
#endif

// Quarter-steps per mechanical detent. A full Gray-code cycle is 4; encoders that
// rest mid-cycle produce 2. Wrong value = double or half counting, which is the
// first thing to check on new hardware.
#ifndef INPUTDRIVER_ENCODER_STEPS_PER_DETENT
#define INPUTDRIVER_ENCODER_STEPS_PER_DETENT 4
#endif

// Swap reported direction without rewiring or swapping the pin macros.
#ifndef INPUTDRIVER_ENCODER_INVERT
#define INPUTDRIVER_ENCODER_INVERT 0
#endif

// 500 rather than the 300 the older rotary driver uses: a knob shaft needs a firmer push than a
// button, so a deliberate click routinely runs past 300 ms and would be misread as a long press.
#ifndef INPUTDRIVER_ENCODER_LONG_PRESS_MS
#define INPUTDRIVER_ENCODER_LONG_PRESS_MS 500
#endif

// Contact bounce on a bare switch. The A/B channels need no equivalent: the
// decoder below rejects bounce structurally rather than by waiting it out.
#ifndef INPUTDRIVER_ENCODER_BTN_DEBOUNCE_MS
#define INPUTDRIVER_ENCODER_BTN_DEBOUNCE_MS 25
#endif

#ifndef INPUTDRIVER_ENCODER_EVENT_CW
#define INPUTDRIVER_ENCODER_EVENT_CW INPUT_BROKER_DOWN
#endif
#ifndef INPUTDRIVER_ENCODER_EVENT_CCW
#define INPUTDRIVER_ENCODER_EVENT_CCW INPUT_BROKER_UP
#endif
#ifndef INPUTDRIVER_ENCODER_EVENT_PRESS
#define INPUTDRIVER_ENCODER_EVENT_PRESS INPUT_BROKER_SELECT
#endif
#ifndef INPUTDRIVER_ENCODER_EVENT_PRESS_LONG
#define INPUTDRIVER_ENCODER_EVENT_PRESS_LONG INPUT_BROKER_SELECT_LONG
#endif

/**
 * One decode step, indexed by (previous << 2) | current of the two-bit (A << 1) | B sample.
 *
 * The four "both channels changed" entries are 0. A real encoder moves one channel at a
 * time, so a simultaneous change means contact bounce or a missed sample; scoring it zero
 * discards it rather than inventing a direction for it.
 *
 * This is why the driver needs no RC filter and no settle timer on A/B. Chatter is an
 * out-and-back along the Gray cycle - 00->10->00->10 scores +1, -1, +1 - so a ringing
 * contact has zero net displacement by construction. A table-driven decoder counts
 * displacement; an edge counter would count noise.
 */
static const int8_t quadratureTransitionTable[16] = {0, -1, +1, 0, +1, 0, 0, -1, -1, 0, 0, +1, 0, +1, -1, 0};

/**
 * Feed one raw sample and report whole detents moved.
 *
 * Free function with caller-held state so the native test suite can drive the decoder
 * without pins, a thread, or the Arduino layer.
 *
 * @param state       persistent two-bit previous sample; seed with the first reading
 * @param accum       persistent sub-detent accumulator
 * @param ab          this sample, (A << 1) | B, already normalised to active-high
 * @param perDetent   quarter-steps per detent (INPUTDRIVER_ENCODER_STEPS_PER_DETENT)
 * @return            -1, 0 or +1 detents
 */
inline int8_t quadratureFeed(uint8_t &state, int8_t &accum, uint8_t ab, uint8_t perDetent)
{
    ab &= 0x03;
    const int8_t step = quadratureTransitionTable[((state << 2) | ab) & 0x0F];
    state = ab;
    if (step == 0)
        return 0;

    if (perDetent < 1)
        perDetent = 1;
    accum += step;

    // Subtract rather than zero, so the residue stays on the correct side of the boundary.
    // Zeroing would let a detent that jitters across the threshold emit on every wobble.
    if (accum >= (int8_t)perDetent) {
        accum -= (int8_t)perDetent;
        return +1;
    }
    if (accum <= -(int8_t)perDetent) {
        accum += (int8_t)perDetent;
        return -1;
    }
    return 0;
}

#if defined(INPUTDRIVER_ENCODER_TYPE) && (INPUTDRIVER_ENCODER_TYPE == 4)

#if !defined(INPUTDRIVER_ENCODER_A) || !defined(INPUTDRIVER_ENCODER_B)
#error "INPUTDRIVER_ENCODER_TYPE 4 needs INPUTDRIVER_ENCODER_A and INPUTDRIVER_ENCODER_B"
#endif

class QuadratureEncoder : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    explicit QuadratureEncoder(const char *name);
    bool init();
    int32_t runOnce() override;

#ifdef ARCH_ESP32
    // Interrupts do not survive light sleep, so drop and re-arm them around it.
    int beforeLightSleep(void *unused);
    int afterLightSleep(esp_sleep_wakeup_cause_t cause);
#endif

  private:
    static void handleInterrupt();
    void sample();
    void attachInterrupts();
    void detachInterrupts();
    static uint8_t readAB();

    const char *_originName;

    // Decoder state is ISR-exclusive; only the detent tally crosses to the thread.
    std::atomic<int16_t> pendingDetents{0};
    uint8_t abState = 0;
    int8_t accum = 0;

#ifdef INPUTDRIVER_ENCODER_BTN
    bool pressed = false;
    bool longFired = false;
    uint32_t pressStartMs = 0;
    uint32_t lastPressChangeMs = 0;
#endif

    static QuadratureEncoder *instance;

#ifdef ARCH_ESP32
    CallbackObserver<QuadratureEncoder, void *> lsObserver =
        CallbackObserver<QuadratureEncoder, void *>(this, &QuadratureEncoder::beforeLightSleep);
    CallbackObserver<QuadratureEncoder, esp_sleep_wakeup_cause_t> lsEndObserver =
        CallbackObserver<QuadratureEncoder, esp_sleep_wakeup_cause_t>(this, &QuadratureEncoder::afterLightSleep);
#endif
};

extern QuadratureEncoder *quadratureEncoder;

#endif // INPUTDRIVER_ENCODER_TYPE == 4
