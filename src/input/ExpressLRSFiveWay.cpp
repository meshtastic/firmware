
#include "ExpressLRSFiveWay.h"

#ifdef INPUTBROKER_EXPRESSLRSFIVEWAY_TYPE

static const char inputSourceName[] = "ExpressLRS5Way"; // should match "allow input source" string

/**
 * @brief Calculate fuzz: half the distance to the next nearest neighbor for each joystick position.
 *
 * The goal is to avoid collisions between joystick positions while still maintaining
 * the widest tolerance for the analog value.
 *
 * Example: {10,50,800,1000,300,1600}
 * If we just choose the minimum difference for this array the value would
 * be 40/2 = 20.
 *
 * 20 does not leave enough room for the joystick position using 1600 which
 * could have a +-100 offset.
 *
 * Example Fuzz values: {20, 20, 100, 100, 125, 300} now the fuzz for the 1600
 * position is 300 instead of 20
 */
void ExpressLRSFiveWay::calcFuzzValues()
{
    for (unsigned int i = 0; i < N_JOY_ADC_VALUES; i++) {
        uint16_t closestDist = 0xffff;
        uint16_t ival = joyAdcValues[i];
        // Find the closest value to ival
        for (unsigned int j = 0; j < N_JOY_ADC_VALUES; j++) {
            // Don't compare value with itself
            if (j == i)
                continue;
            uint16_t jval = joyAdcValues[j];
            if (jval < ival && (ival - jval < closestDist))
                closestDist = ival - jval;
            if (jval > ival && (jval - ival < closestDist))
                closestDist = jval - ival;
        } // for j

        // And the fuzz is half the distance to the closest value
        fuzzValues[i] = closestDist / 2;
        // DBG("joy%u=%u f=%u, ", i, ival, fuzzValues[i]);
    } // for i
}

int ExpressLRSFiveWay::readKey()
{
    uint16_t value = analogRead(PIN_JOYSTICK);

    constexpr uint8_t IDX_TO_INPUT[N_JOY_ADC_VALUES - 1] = {INPUT_KEY_UP_PRESS, INPUT_KEY_DOWN_PRESS, INPUT_KEY_LEFT_PRESS,
                                                            INPUT_KEY_RIGHT_PRESS, INPUT_KEY_OK_PRESS};
    for (unsigned int i = 0; i < N_JOY_ADC_VALUES - 1; ++i) {
        if (value < (joyAdcValues[i] + fuzzValues[i]) && value > (joyAdcValues[i] - fuzzValues[i]))
            return IDX_TO_INPUT[i];
    }
    return INPUT_KEY_NO_PRESS;
}

ExpressLRSFiveWay::ExpressLRSFiveWay() : concurrency::OSThread(inputSourceName) // Meshtastic: derive from threading class
{
    isLongPressed = false;
    keyInProcess = INPUT_KEY_NO_PRESS;
    keyDownStart = 0;

    calcFuzzValues();

    inputBroker->registerSource(this); // Meshtastic: register with canned messages
}

void ExpressLRSFiveWay::update(int *keyValue, bool *keyLongPressed)
{
    *keyValue = INPUT_KEY_NO_PRESS;

    int newKey = readKey();
    uint32_t now = millis();
    if (keyInProcess == INPUT_KEY_NO_PRESS) {
        // New key down
        if (newKey != INPUT_KEY_NO_PRESS) {
            keyDownStart = now;
            // DBGLN("down=%u", newKey);
        }
    } else {
        // if key released
        if (newKey == INPUT_KEY_NO_PRESS) {
            // DBGLN("up=%u", keyInProcess);
            if (!isLongPressed) {
                if ((now - keyDownStart) > KEY_DEBOUNCE_MS) {
                    *keyValue = keyInProcess;
                    *keyLongPressed = false;
                }
            }
            isLongPressed = false;
        }
        // else if the key has changed while down, reset state for next go-around
        else if (newKey != keyInProcess) {
            newKey = INPUT_KEY_NO_PRESS;
        }
        // else still pressing, waiting for long if not already signaled
        else if (!isLongPressed) {
            if ((now - keyDownStart) > KEY_LONG_PRESS_MS) {
                *keyValue = keyInProcess;
                *keyLongPressed = true;
                isLongPressed = true;
            }
        }
    } // if keyInProcess != INPUT_KEY_NO_PRESS

    keyInProcess = newKey;
}

int32_t ExpressLRSFiveWay::runOnce()
{
    uint32_t now = millis();

    int keyValue;
    bool longPressed;
    update(&keyValue, &longPressed);

    switch (keyValue) {
    // Allow left and right keys to close the canned message frame
    case ExpressLRSFiveWay::INPUT_KEY_LEFT_PRESS:
    case ExpressLRSFiveWay::INPUT_KEY_RIGHT_PRESS:
        if (cannedMessageModule->shouldDraw())
            raiseEvent(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_CANCEL);
        else
            raiseEvent((_meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar)keyValue);
        break;
    case ExpressLRSFiveWay::INPUT_KEY_UP_PRESS:
    case ExpressLRSFiveWay::INPUT_KEY_DOWN_PRESS:
        raiseEvent((_meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar)keyValue);
        break;
    case ExpressLRSFiveWay::INPUT_KEY_OK_PRESS:
        // Canned message module interprets SELECT as user button short press when suitable,
        // but doesn't handle long press, so we'll do that here
        if (longPressed) {
            LOG_INFO("Shutdown from long press\n");
            power->shutdown();
        } else
            raiseEvent(meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_SELECT);
        break;
    case ExpressLRSFiveWay::INPUT_KEY_NO_PRESS:
    default:
        break;
    }

    // If there has been recent key activity, poll the joystick slightly more frequently
    if (now < keyDownStart + (20 * 1000UL)) // Within last 20 seconds
        return 100;

    // Otherwise, poll slightly less often
    // Too many missed pressed if much slower than 250ms
    return 250;
}

// Feed input to the canned messages module
void ExpressLRSFiveWay::raiseEvent(_meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar key)
{
    InputEvent e;
    e.source = inputSourceName;
    e.inputEvent = key;
    notifyObservers(&e);
}

ExpressLRSFiveWay *expressLRSFiveWayInput = nullptr;

#endif