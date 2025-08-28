#include "UpDownInterruptBase.h"
#include "configuration.h"

UpDownInterruptBase::UpDownInterruptBase(const char *name) : concurrency::OSThread(name)
{
    this->_originName = name;
}

void UpDownInterruptBase::init(uint8_t pinDown, uint8_t pinUp, uint8_t pinPress, input_broker_event eventDown,
                               input_broker_event eventUp, input_broker_event eventPressed, input_broker_event eventPressedLong,
                               input_broker_event eventUpLong, input_broker_event eventDownLong, void (*onIntDown)(),
                               void (*onIntUp)(), void (*onIntPress)(), unsigned long updownDebounceMs)
{
    this->_pinDown = pinDown;
    this->_pinUp = pinUp;
    this->_pinPress = pinPress;
    this->_eventDown = eventDown;
    this->_eventUp = eventUp;
    this->_eventPressed = eventPressed;
    this->_eventPressedLong = eventPressedLong;
    this->_eventUpLong = eventUpLong;
    this->_eventDownLong = eventDownLong;

    // Store debounce configuration passed by caller
    this->updownDebounceMs = updownDebounceMs;
    bool isRAK = false;
#ifdef RAK_4631
    isRAK = true;
#endif

    if (!isRAK || pinPress != 0) {
        pinMode(pinPress, INPUT_PULLUP);
        attachInterrupt(pinPress, onIntPress, FALLING);
    }
    if (!isRAK || this->_pinDown != 0) {
        pinMode(this->_pinDown, INPUT_PULLUP);
        attachInterrupt(this->_pinDown, onIntDown, FALLING);
    }
    if (!isRAK || this->_pinUp != 0) {
        pinMode(this->_pinUp, INPUT_PULLUP);
        attachInterrupt(this->_pinUp, onIntUp, FALLING);
    }

    LOG_DEBUG("Up/down/press GPIO initialized (%d, %d, %d)", this->_pinUp, this->_pinDown, pinPress);

    this->setInterval(20);
}

int32_t UpDownInterruptBase::runOnce()
{
    InputEvent e;
    e.inputEvent = INPUT_BROKER_NONE;
    unsigned long now = millis();

    // Read all button states once at the beginning
    bool pressButtonPressed = !digitalRead(_pinPress);
    bool upButtonPressed = !digitalRead(_pinUp);
    bool downButtonPressed = !digitalRead(_pinDown);

    // Handle initial button press detection - only if not already detected
    if (this->action == UPDOWN_ACTION_PRESSED && pressButtonPressed && !pressDetected) {
        pressDetected = true;
        pressStartTime = now;
    } else if (this->action == UPDOWN_ACTION_UP && upButtonPressed && !upDetected) {
        upDetected = true;
        upStartTime = now;
    } else if (this->action == UPDOWN_ACTION_DOWN && downButtonPressed && !downDetected) {
        downDetected = true;
        downStartTime = now;
    }

    // Handle long press detection for press button
    if (pressDetected && pressStartTime > 0) {
        uint32_t pressDuration = now - pressStartTime;

        if (!pressButtonPressed) {
            // Button released
            if (pressDuration < LONG_PRESS_DURATION && now - lastPressKeyTime >= pressDebounceMs) {
                lastPressKeyTime = now;
                e.inputEvent = this->_eventPressed;
            }
            // Reset state
            pressDetected = false;
            pressStartTime = 0;
            lastPressLongEventTime = 0;
        } else if (pressDuration >= LONG_PRESS_DURATION && lastPressLongEventTime == 0) {
            // First long press event only - avoid repeated events causing lag
            e.inputEvent = this->_eventPressedLong;
            lastPressLongEventTime = now;
        }
    }

    // Handle long press detection for up button
    if (upDetected && upStartTime > 0) {
        uint32_t upDuration = now - upStartTime;

        if (!upButtonPressed) {
            // Button released
            if (upDuration < LONG_PRESS_DURATION && now - lastUpKeyTime >= updownDebounceMs) {
                lastUpKeyTime = now;
                e.inputEvent = this->_eventUp;
            }
            // Reset state
            upDetected = false;
            upStartTime = 0;
            lastUpLongEventTime = 0;
        } else if (upDuration >= LONG_PRESS_DURATION) {
            // Auto-repeat long press events
            if (lastUpLongEventTime == 0 || (now - lastUpLongEventTime) >= LONG_PRESS_REPEAT_INTERVAL) {
                e.inputEvent = this->_eventUpLong;
                lastUpLongEventTime = now;
            }
        }
    }

    // Handle long press detection for down button
    if (downDetected && downStartTime > 0) {
        uint32_t downDuration = now - downStartTime;

        if (!downButtonPressed) {
            // Button released
            if (downDuration < LONG_PRESS_DURATION && now - lastDownKeyTime >= updownDebounceMs) {
                lastDownKeyTime = now;
                e.inputEvent = this->_eventDown;
            }
            // Reset state
            downDetected = false;
            downStartTime = 0;
            lastDownLongEventTime = 0;
        } else if (downDuration >= LONG_PRESS_DURATION) {
            // Auto-repeat long press events
            if (lastDownLongEventTime == 0 || (now - lastDownLongEventTime) >= LONG_PRESS_REPEAT_INTERVAL) {
                e.inputEvent = this->_eventDownLong;
                lastDownLongEventTime = now;
            }
        }
    }

    if (e.inputEvent != INPUT_BROKER_NONE) {
        e.source = this->_originName;
        e.kbchar = INPUT_BROKER_NONE;
        this->notifyObservers(&e);
    }

    if (!pressDetected && !upDetected && !downDetected) {
        this->action = UPDOWN_ACTION_NONE;
    }

    return 20; // This will control how the input frequency
}

void UpDownInterruptBase::intPressHandler()
{
    this->action = UPDOWN_ACTION_PRESSED;
}

void UpDownInterruptBase::intDownHandler()
{
    this->action = UPDOWN_ACTION_DOWN;
}

void UpDownInterruptBase::intUpHandler()
{
    this->action = UPDOWN_ACTION_UP;
}
