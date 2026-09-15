#include "QuadratureEncoder.h"

#if defined(INPUTDRIVER_ENCODER_TYPE) && (INPUTDRIVER_ENCODER_TYPE == 4)

#include "Throttle.h"
#include "main.h"
#include "sleep.h"

QuadratureEncoder *quadratureEncoder;
QuadratureEncoder *QuadratureEncoder::instance = nullptr;

extern bool osk_found;

QuadratureEncoder::QuadratureEncoder(const char *name) : concurrency::OSThread(name), _originName(name) {}

uint8_t QuadratureEncoder::readAB()
{
    uint8_t a = digitalRead(INPUTDRIVER_ENCODER_A) ? 1 : 0;
    uint8_t b = digitalRead(INPUTDRIVER_ENCODER_B) ? 1 : 0;
#if INPUTDRIVER_ENCODER_ACTIVE_LOW
    a ^= 1;
    b ^= 1;
#endif
    return (uint8_t)((a << 1) | b);
}

// Decode in the ISR and bank whole detents only. A missed edge is unrecoverable state loss, so
// the table has to run on every edge; keeping the sub-detent accumulator here as well means a
// fast spin cannot lose counts to scheduling latency. It is a table index and an add.
void QuadratureEncoder::sample()
{
    const int8_t detents = quadratureFeed(abState, accum, readAB(), INPUTDRIVER_ENCODER_STEPS_PER_DETENT);
    if (detents != 0)
        pendingDetents.fetch_add(detents, std::memory_order_relaxed);
}

void QuadratureEncoder::handleInterrupt()
{
    if (!instance)
        return;
    instance->sample();
    instance->setIntervalFromNow(0);
    runASAP = true;
#if defined(HAS_FREE_RTOS) && !defined(ARCH_RP2040)
    BaseType_t higherWake = 0;
    concurrency::mainDelay.interruptFromISR(&higherWake);
#endif
}

void QuadratureEncoder::attachInterrupts()
{
    attachInterrupt(INPUTDRIVER_ENCODER_A, QuadratureEncoder::handleInterrupt, CHANGE);
    attachInterrupt(INPUTDRIVER_ENCODER_B, QuadratureEncoder::handleInterrupt, CHANGE);
#ifdef INPUTDRIVER_ENCODER_BTN
    attachInterrupt(INPUTDRIVER_ENCODER_BTN, QuadratureEncoder::handleInterrupt, CHANGE);
#endif
}

void QuadratureEncoder::detachInterrupts()
{
    detachInterrupt(INPUTDRIVER_ENCODER_A);
    detachInterrupt(INPUTDRIVER_ENCODER_B);
#ifdef INPUTDRIVER_ENCODER_BTN
    detachInterrupt(INPUTDRIVER_ENCODER_BTN);
#endif
}

bool QuadratureEncoder::init()
{
    instance = this;

#if INPUTDRIVER_ENCODER_PULLUP
    pinMode(INPUTDRIVER_ENCODER_A, INPUT_PULLUP);
    pinMode(INPUTDRIVER_ENCODER_B, INPUT_PULLUP);
#else
    pinMode(INPUTDRIVER_ENCODER_A, INPUT);
    pinMode(INPUTDRIVER_ENCODER_B, INPUT);
#endif

#ifdef INPUTDRIVER_ENCODER_BTN
#if !INPUTDRIVER_ENCODER_PULLUP
    pinMode(INPUTDRIVER_ENCODER_BTN, INPUT);
#elif INPUTDRIVER_ENCODER_BTN_ACTIVE_LOW
    pinMode(INPUTDRIVER_ENCODER_BTN, INPUT_PULLUP);
#else
    // Idles low, so a pull-up would fight the module's own resistor and hold the line asserted.
    pinMode(INPUTDRIVER_ENCODER_BTN, INPUT_PULLDOWN);
#endif
#endif

    // Seed from the resting position, so the first movement is scored against where the shaft
    // actually is rather than against an assumed zero.
    abState = readAB();
    accum = 0;

    attachInterrupts();

#ifdef ARCH_ESP32
    lsObserver.observe(&notifyLightSleep);
    lsEndObserver.observe(&notifyLightSleepEnd);
#endif

    inputBroker->registerSource(this);
#ifndef HAS_PHYSICAL_KEYBOARD
    osk_found = true;
#endif

#ifdef INPUTDRIVER_ENCODER_BTN
    LOG_INFO("Quadrature encoder A=%d B=%d btn=%d (%d steps/detent)", INPUTDRIVER_ENCODER_A, INPUTDRIVER_ENCODER_B,
             INPUTDRIVER_ENCODER_BTN, INPUTDRIVER_ENCODER_STEPS_PER_DETENT);
#else
    LOG_INFO("Quadrature encoder A=%d B=%d, no switch (%d steps/detent)", INPUTDRIVER_ENCODER_A, INPUTDRIVER_ENCODER_B,
             INPUTDRIVER_ENCODER_STEPS_PER_DETENT);
#endif
    return true;
}

int32_t QuadratureEncoder::runOnce()
{
    InputEvent e = {.source = _originName, .inputEvent = INPUT_BROKER_NONE, .kbchar = 0, .touchX = 0, .touchY = 0};

    // exchange, not load-then-store: a detent arriving from the ISR mid-drain must not be lost.
    int16_t detents = pendingDetents.exchange(0, std::memory_order_relaxed);
#if INPUTDRIVER_ENCODER_INVERT
    detents = -detents;
#endif
    // One event per detent, so a fast spin scrolls by the same amount it would have if every
    // detent had been serviced individually.
    while (detents != 0) {
        const int8_t dir = (detents > 0) ? +1 : -1;
        detents -= dir;
        e.inputEvent = (dir > 0) ? INPUTDRIVER_ENCODER_EVENT_CW : INPUTDRIVER_ENCODER_EVENT_CCW;
        if (e.inputEvent != INPUT_BROKER_NONE)
            notifyObservers(&e);
    }
    e.inputEvent = INPUT_BROKER_NONE;

#ifdef INPUTDRIVER_ENCODER_BTN
    const uint32_t now = millis();
    bool level = digitalRead(INPUTDRIVER_ENCODER_BTN) ? true : false;
#if INPUTDRIVER_ENCODER_BTN_ACTIVE_LOW
    level = !level;
#endif

    if (level != pressed && Throttle::hasElapsed(lastPressChangeMs, INPUTDRIVER_ENCODER_BTN_DEBOUNCE_MS)) {
        lastPressChangeMs = now;
        pressed = level;
        if (pressed) {
            pressStartMs = now;
            longFired = false;
        } else if (!longFired) {
            // A short press only resolves on release - until then it might still become a long one.
            e.inputEvent = INPUTDRIVER_ENCODER_EVENT_PRESS;
            if (e.inputEvent != INPUT_BROKER_NONE)
                notifyObservers(&e);
            e.inputEvent = INPUT_BROKER_NONE;
        }
    }

    if (pressed && !longFired && Throttle::hasElapsed(pressStartMs, INPUTDRIVER_ENCODER_LONG_PRESS_MS)) {
        // Single shot: a rotary select should not auto-repeat while held.
        longFired = true;
        e.inputEvent = INPUTDRIVER_ENCODER_EVENT_PRESS_LONG;
        if (e.inputEvent != INPUT_BROKER_NONE)
            notifyObservers(&e);
    }

    // Keep polling while held so the long press can mature without another edge.
    if (pressed)
        return 20;
#endif

    return INT32_MAX;
}

#ifdef ARCH_ESP32
int QuadratureEncoder::beforeLightSleep(void *unused)
{
    detachInterrupts();
    return 0;
}

int QuadratureEncoder::afterLightSleep(esp_sleep_wakeup_cause_t cause)
{
    // Re-seed before re-arming: the shaft can be turned while we are not watching, and decoding
    // against a stale sample would invent a detent that never happened.
    abState = readAB();
    accum = 0;
    attachInterrupts();
    return 0;
}
#endif

#endif // INPUTDRIVER_ENCODER_TYPE == 4
