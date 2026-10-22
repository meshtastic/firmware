#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_INPUTBROKER

#include "KeypadInput.h"
#include "UptimeClock.h"
#include "mesh/Throttle.h"

KeypadInput::KeypadInput(ReadState readState) : concurrency::OSThread("KeypadInput", POLL_INTERVAL_MS), readState(readState) {}

bool KeypadInput::init()
{
    if (!inputBroker || !readState || !readState(sampledKeys)) {
        disable();
        return false;
    }

    sampledKeys &= KEY_MASK;
    const uint32_t nowMs = Time::getMillis();
    for (uint8_t keyIndex = 0; keyIndex < KEY_COUNT; keyIndex++)
        changedAtMs[keyIndex] = nowMs;

    inputBroker->registerSource(this);
    ready = true;
    return true;
}

int32_t KeypadInput::runOnce()
{
    if (!ready)
        return disable();

    uint8_t pressedKeys = 0;
    if (!readState(pressedKeys)) {
        sampledKeys = stableKeys;
        return POLL_INTERVAL_MS;
    }

    pressedKeys &= KEY_MASK;
    const uint32_t nowMs = Time::getMillis();
    for (uint8_t keyIndex = 0; keyIndex < KEY_COUNT; keyIndex++) {
        const uint8_t keyMask = 1U << keyIndex;
        if ((pressedKeys ^ sampledKeys) & keyMask)
            changedAtMs[keyIndex] = nowMs;

        if (((pressedKeys ^ stableKeys) & keyMask) && Throttle::hasElapsed(changedAtMs[keyIndex], DEBOUNCE_MS)) {
            stableKeys ^= keyMask;
            repeatingKeys &= ~keyMask;
            if (stableKeys & keyMask) {
                emitEvent(keyIndex);
                if (keyIndex < 4)
                    repeatAtMs[keyIndex] = nowMs;
            }
        }

        if (keyIndex < 4 && (stableKeys & pressedKeys & keyMask)) {
            const uint32_t intervalMs = (repeatingKeys & keyMask) ? REPEAT_INTERVAL_MS : REPEAT_DELAY_MS;
            if (Throttle::hasElapsed(repeatAtMs[keyIndex], intervalMs)) {
                emitEvent(keyIndex);
                repeatingKeys |= keyMask;
                repeatAtMs[keyIndex] = nowMs;
            }
        }
    }
    sampledKeys = pressedKeys;
    return POLL_INTERVAL_MS;
}

void KeypadInput::emitEvent(uint8_t keyIndex)
{
    static constexpr input_broker_event KEY_EVENTS[KEY_COUNT] = {INPUT_BROKER_UP,    INPUT_BROKER_DOWN,   INPUT_BROKER_LEFT,
                                                                 INPUT_BROKER_RIGHT, INPUT_BROKER_SELECT, INPUT_BROKER_CANCEL};
    InputEvent event = {};
    event.source = "KeypadInput";
    event.inputEvent = KEY_EVENTS[keyIndex];
    notifyObservers(&event);
}

#endif