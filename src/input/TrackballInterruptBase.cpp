#include "TrackballInterruptBase.h"
#include "Throttle.h"
#include "configuration.h"

extern bool osk_found;

TrackballInterruptBase::TrackballInterruptBase(const char *name) : concurrency::OSThread(name), _originName(name) {}

void TrackballInterruptBase::init(uint8_t pinDown, uint8_t pinUp, uint8_t pinLeft, uint8_t pinRight, uint8_t pinPress,
                                  input_broker_event eventDown, input_broker_event eventUp, input_broker_event eventLeft,
                                  input_broker_event eventRight, input_broker_event eventPressed,
                                  input_broker_event eventPressedLong, void (*onIntDown)(), void (*onIntUp)(),
                                  void (*onIntLeft)(), void (*onIntRight)(), void (*onIntPress)())
{
    this->_pinDown = pinDown;
    this->_pinUp = pinUp;
    this->_pinLeft = pinLeft;
    this->_pinRight = pinRight;
    this->_pinPress = pinPress;
    this->_eventDown = eventDown;
    this->_eventUp = eventUp;
    this->_eventLeft = eventLeft;
    this->_eventRight = eventRight;
    this->_eventPressed = eventPressed;
    this->_eventPressedLong = eventPressedLong;

    if (pinPress != 255) {
        pinMode(pinPress, INPUT_PULLUP);
        attachInterrupt(pinPress, onIntPress, TB_DIRECTION);
    }
    if (this->_pinDown != 255) {
        pinMode(this->_pinDown, INPUT_PULLUP);
        attachInterrupt(this->_pinDown, onIntDown, TB_DIRECTION);
    }
    if (this->_pinUp != 255) {
        pinMode(this->_pinUp, INPUT_PULLUP);
        attachInterrupt(this->_pinUp, onIntUp, TB_DIRECTION);
    }
    if (this->_pinLeft != 255) {
        pinMode(this->_pinLeft, INPUT_PULLUP);
        attachInterrupt(this->_pinLeft, onIntLeft, TB_DIRECTION);
    }
    if (this->_pinRight != 255) {
        pinMode(this->_pinRight, INPUT_PULLUP);
        attachInterrupt(this->_pinRight, onIntRight, TB_DIRECTION);
    }

    LOG_DEBUG("Trackball GPIO initialized - UP:%d DOWN:%d LEFT:%d RIGHT:%d PRESS:%d", this->_pinUp, this->_pinDown,
              this->_pinLeft, this->_pinRight, pinPress);
#ifndef HAS_PHYSICAL_KEYBOARD
    osk_found = true;
#endif
    this->setInterval(100);
}

int32_t TrackballInterruptBase::runOnce()
{
    InputEvent e = {};
    e.inputEvent = INPUT_BROKER_NONE;
#if TB_THRESHOLD
    if (lastInterruptTime && !Throttle::isWithinTimespanMs(lastInterruptTime, 1000)) {
        left_counter = 0;
        right_counter = 0;
        up_counter = 0;
        down_counter = 0;
        lastInterruptTime = 0;
    }
#ifdef INPUT_DEBUG
    if (left_counter > 0 || right_counter > 0 || up_counter > 0 || down_counter > 0) {
        LOG_DEBUG("L %u R %u U %u D %u, time %u", left_counter, right_counter, up_counter, down_counter, millis());
    }
#endif
#endif

    // Handle long press detection for press button
    if (pressDetected && pressStartTime > 0) {
        uint32_t pressDuration = millis() - pressStartTime;
        bool buttonStillPressed = false;

        buttonStillPressed = !digitalRead(_pinPress);

        if (!buttonStillPressed) {
            // Button released
            if (pressDuration < LONG_PRESS_DURATION) {
                // Short press
                e.inputEvent = this->_eventPressed;
            }
            // Reset state
            pressDetected = false;
            pressStartTime = 0;
            lastLongPressEventTime = 0;
            this->action = TB_ACTION_NONE;
        } else if (pressDuration >= LONG_PRESS_DURATION) {
            // Long press detected
            uint32_t currentTime = millis();
            // Only trigger long press event if enough time has passed since the last one
            if (lastLongPressEventTime == 0 || (currentTime - lastLongPressEventTime) >= LONG_PRESS_REPEAT_INTERVAL) {
                e.inputEvent = this->_eventPressedLong;
                lastLongPressEventTime = currentTime;
            }
            this->action = TB_ACTION_PRESSED_LONG;
        }
    }

    if (directionDetected && directionStartTime > 0) {
        uint32_t directionDuration = millis() - directionStartTime;
        uint8_t directionPressedNow = 0;
        directionInterval++;

        if (!digitalRead(_pinUp)) {
            directionPressedNow = TB_ACTION_UP;
        } else if (!digitalRead(_pinDown)) {
            directionPressedNow = TB_ACTION_DOWN;
        } else if (!digitalRead(_pinLeft)) {
            directionPressedNow = TB_ACTION_LEFT;
        } else if (!digitalRead(_pinRight)) {
            directionPressedNow = TB_ACTION_RIGHT;
        }

        const uint8_t DIRECTION_REPEAT_THRESHOLD = 3;

        if (directionPressedNow == TB_ACTION_NONE) {
            // Reset state
            directionDetected = false;
            directionStartTime = 0;
            directionInterval = 0;
            this->action = TB_ACTION_NONE;
        } else if (directionDuration >= LONG_PRESS_DURATION && directionInterval >= DIRECTION_REPEAT_THRESHOLD) {
            // repeat event when long press these direction.
            switch (directionPressedNow) {
            case TB_ACTION_UP:
                e.inputEvent = this->_eventUp;
                break;
            case TB_ACTION_DOWN:
                e.inputEvent = this->_eventDown;
                break;
            case TB_ACTION_LEFT:
                e.inputEvent = this->_eventLeft;
                break;
            case TB_ACTION_RIGHT:
                e.inputEvent = this->_eventRight;
                break;
            }

            directionInterval = 0;
        }
    }

#if TB_THRESHOLD
    if (this->action == TB_ACTION_PRESSED && (!pressDetected || pressStartTime == 0)) {
        // Start long press detection
        pressDetected = true;
        pressStartTime = millis();
        // Don't send event yet, wait to see if it's a long press
    } else if (up_counter >= TB_THRESHOLD) {
#ifdef INPUT_DEBUG
        LOG_DEBUG("Trackball event UP %u", millis());
#endif
        e.inputEvent = this->_eventUp;
    } else if (down_counter >= TB_THRESHOLD) {
#ifdef INPUT_DEBUG
        LOG_DEBUG("Trackball event DOWN %u", millis());
#endif
        e.inputEvent = this->_eventDown;
    } else if (left_counter >= TB_THRESHOLD) {
#ifdef INPUT_DEBUG
        LOG_DEBUG("Trackball event LEFT %u", millis());
#endif
        e.inputEvent = this->_eventLeft;
    } else if (right_counter >= TB_THRESHOLD) {
#ifdef INPUT_DEBUG
        LOG_DEBUG("Trackball event RIGHT %u", millis());
#endif
        e.inputEvent = this->_eventRight;
    }
#else
    if (this->action == TB_ACTION_PRESSED && !digitalRead(_pinPress) && !pressDetected) {
        // Start long press detection
        pressDetected = true;
        pressStartTime = millis();
        // Don't send event yet, wait to see if it's a long press
    } else if (this->action == TB_ACTION_UP && !digitalRead(_pinUp) && !directionDetected) {
        directionDetected = true;
        directionStartTime = millis();
        e.inputEvent = this->_eventUp;
        // send event first,will automatically trigger every 50ms * 3 after 500ms
    } else if (this->action == TB_ACTION_DOWN && !digitalRead(_pinDown) && !directionDetected) {
        directionDetected = true;
        directionStartTime = millis();
        e.inputEvent = this->_eventDown;
    } else if (this->action == TB_ACTION_LEFT && !digitalRead(_pinLeft) && !directionDetected) {
        directionDetected = true;
        directionStartTime = millis();
        e.inputEvent = this->_eventLeft;
    } else if (this->action == TB_ACTION_RIGHT && !digitalRead(_pinRight) && !directionDetected) {
        directionDetected = true;
        directionStartTime = millis();
        e.inputEvent = this->_eventRight;
    }
#endif

    if (e.inputEvent != INPUT_BROKER_NONE) {
        e.source = this->_originName;
        e.kbchar = 0x00;
        this->notifyObservers(&e);
#if TB_THRESHOLD
        left_counter = 0;
        right_counter = 0;
        up_counter = 0;
        down_counter = 0;
#endif
    }

    // Only update lastEvent for non-press actions or completed press actions
    if (this->action != TB_ACTION_PRESSED || !pressDetected) {
        lastEvent = action;
        if (!pressDetected) {
            this->action = TB_ACTION_NONE;
        }
    }

    return 50; // Check more frequently for better long press detection
}

void TrackballInterruptBase::intPressHandler()
{
    if (!Throttle::isWithinTimespanMs(lastInterruptTime, 10))
        this->action = TB_ACTION_PRESSED;
    lastInterruptTime = millis();
}

void TrackballInterruptBase::intDownHandler()
{
    if (TB_THRESHOLD || !Throttle::isWithinTimespanMs(lastInterruptTime, 10))
        this->action = TB_ACTION_DOWN;
    lastInterruptTime = millis();

#if TB_THRESHOLD
    down_counter++;
#endif
}

void TrackballInterruptBase::intUpHandler()
{
    if (TB_THRESHOLD || !Throttle::isWithinTimespanMs(lastInterruptTime, 10))
        this->action = TB_ACTION_UP;
    lastInterruptTime = millis();

#if TB_THRESHOLD
    up_counter++;
#endif
}

void TrackballInterruptBase::intLeftHandler()
{
    if (TB_THRESHOLD || !Throttle::isWithinTimespanMs(lastInterruptTime, 10))
        this->action = TB_ACTION_LEFT;
    lastInterruptTime = millis();
#if TB_THRESHOLD
    left_counter++;
#endif
}

void TrackballInterruptBase::intRightHandler()
{
    if (TB_THRESHOLD || !Throttle::isWithinTimespanMs(lastInterruptTime, 10))
        this->action = TB_ACTION_RIGHT;
    lastInterruptTime = millis();
#if TB_THRESHOLD
    right_counter++;
#endif
}
