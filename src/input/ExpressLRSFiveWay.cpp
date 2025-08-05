#include "ExpressLRSFiveWay.h"
#include "Throttle.h"

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

    constexpr uint8_t IDX_TO_INPUT[N_JOY_ADC_VALUES - 1] = {UP, DOWN, LEFT, RIGHT, OK};
    for (unsigned int i = 0; i < N_JOY_ADC_VALUES - 1; ++i) {
        if (value < (joyAdcValues[i] + fuzzValues[i]) && value > (joyAdcValues[i] - fuzzValues[i]))
            return IDX_TO_INPUT[i];
    }
    return NO_PRESS;
}

ExpressLRSFiveWay::ExpressLRSFiveWay() : concurrency::OSThread(inputSourceName)
{
    // ExpressLRS: init values
    isLongPressed = false;
    keyInProcess = NO_PRESS;
    keyDownStart = 0;

    // Express LRS: calculate the threshold for interpreting ADC values as various buttons
    calcFuzzValues();

    // Meshtastic: register with canned messages
    inputBroker->registerSource(this);
}

// ExpressLRS: interpret reading as key events
void ExpressLRSFiveWay::update(int *keyValue, bool *keyLongPressed)
{
    *keyValue = NO_PRESS;

    int newKey = readKey();
    if (keyInProcess == NO_PRESS) {
        // New key down
        if (newKey != NO_PRESS) {
            keyDownStart = millis();
            // DBGLN("down=%u", newKey);
        }
    } else {
        // if key released
        if (newKey == NO_PRESS) {
            // DBGLN("up=%u", keyInProcess);
            if (!isLongPressed) {
                if (!Throttle::isWithinTimespanMs(keyDownStart, KEY_DEBOUNCE_MS)) {
                    *keyValue = keyInProcess;
                    *keyLongPressed = false;
                }
            }
            isLongPressed = false;
        }
        // else if the key has changed while down, reset state for next go-around
        else if (newKey != keyInProcess) {
            newKey = NO_PRESS;
        }
        // else still pressing, waiting for long if not already signaled
        else if (!isLongPressed) {
            if (!Throttle::isWithinTimespanMs(keyDownStart, KEY_LONG_PRESS_MS)) {
                *keyValue = keyInProcess;
                *keyLongPressed = true;
                isLongPressed = true;
            }
        }
    } // if keyInProcess != NO_PRESS

    keyInProcess = newKey;
}

// Meshtastic: runs at regular intervals
int32_t ExpressLRSFiveWay::runOnce()
{
    uint32_t now = millis();

    // Dismiss any alert frames after 2 seconds
    // Feedback for GPS toggle / adhoc ping
    if (alerting && now > alertingSinceMs + 2000) {
        alerting = false;
        screen->endAlert();
    }

    // Get key events from ExpressLRS code
    int keyValue;
    bool longPressed;
    update(&keyValue, &longPressed);

    // Do something about this key press
    determineAction((KeyType)keyValue, longPressed ? LONG : SHORT);

    // If there has been recent key activity, poll the joystick slightly more frequently
    if (now < keyDownStart + (20 * 1000UL)) // Within last 20 seconds
        return 100;

    // Otherwise, poll slightly less often
    // Too many missed pressed if much slower than 250ms
    return 250;
}

// Determine what action to take when a button press is detected
// Written verbose for easier remapping by user
void ExpressLRSFiveWay::determineAction(KeyType key, PressLength length)
{
    switch (key) {
    case LEFT:
        if (inCannedMessageMenu())        // If in canned message menu
            sendKey(INPUT_BROKER_CANCEL); // exit the menu (press imaginary cancel key)
        else
            sendKey(INPUT_BROKER_LEFT);
        break;

    case RIGHT:
        if (inCannedMessageMenu())        // If in canned message menu:
            sendKey(INPUT_BROKER_CANCEL); // exit the menu (press imaginary cancel key)
        else
            sendKey(INPUT_BROKER_RIGHT);
        break;

    case UP:
        if (length == LONG)
            toggleGPS();
        else
            sendKey(INPUT_BROKER_UP);
        break;

    case DOWN:
        if (length == LONG)
            sendAdhocPing();
        else
            sendKey(INPUT_BROKER_DOWN);
        break;

    case OK:
        if (length == LONG)
            shutdown();
        else
            click(); // Use instead of sendKey(OK). Works better when canned message module disabled
        break;

    default:
        break;
    }
}

// Feed input to the canned messages module
void ExpressLRSFiveWay::sendKey(input_broker_event key)
{
    InputEvent e;
    e.source = inputSourceName;
    e.inputEvent = key;
    notifyObservers(&e);
}

// Enable or Disable a connected GPS
// Contained as one method for easier remapping of buttons by user
void ExpressLRSFiveWay::toggleGPS()
{
#if HAS_GPS && !MESHTASTIC_EXCLUDE_GPS
    if (gps != nullptr) {
        gps->toggleGpsMode();
        screen->startAlert("GPS Toggled");
        alerting = true;
        alertingSinceMs = millis();
    }
#endif
}

// Send either node-info or position, on demand
// Contained as one method for easier remapping of buttons by user
void ExpressLRSFiveWay::sendAdhocPing()
{
    service->refreshLocalMeshNode();
    bool sentPosition = service->trySendPosition(NODENUM_BROADCAST, true);

    // Show custom alert frame, with multi-line centering
    screen->startAlert([sentPosition](OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) -> void {
        uint16_t x_offset = display->width() / 2;
        uint16_t y_offset = 26; // Same constant as the default startAlert frame
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->setFont(FONT_MEDIUM);
        display->drawString(x_offset + x, y_offset + y, "Sent ad-hoc");
        display->drawString(x_offset + x, y_offset + FONT_HEIGHT_MEDIUM + y, sentPosition ? "position" : "nodeinfo");
    });

    alerting = true;
    alertingSinceMs = millis();
}

// Shutdown the node (enter deep-sleep)
// Contained as one method for easier remapping of buttons by user
void ExpressLRSFiveWay::shutdown()
{
    sendKey(INPUT_BROKER_SHUTDOWN);
}

void ExpressLRSFiveWay::click()
{
    sendKey(INPUT_BROKER_SELECT);
}

ExpressLRSFiveWay *expressLRSFiveWayInput = nullptr;

#endif
