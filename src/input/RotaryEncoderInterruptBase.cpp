#include "RotaryEncoderInterruptBase.h"
#include "configuration.h"

RotaryEncoderInterruptBase::RotaryEncoderInterruptBase(const char *name) : concurrency::OSThread(name)
{
    this->_originName = name;
}

void RotaryEncoderInterruptBase::init(
    uint8_t pinA, uint8_t pinB, uint8_t pinPress, input_broker_event eventCw, input_broker_event eventCcw,
    input_broker_event eventPressed, input_broker_event eventPressedLong,
    //    std::function<void(void)> onIntA, std::function<void(void)> onIntB, std::function<void(void)> onIntPress) :
    void (*onIntA)(), void (*onIntB)(), void (*onIntPress)())
{
    this->_pinA = pinA;
    this->_pinB = pinB;
    this->_pinPress = pinPress;
    this->_eventCw = eventCw;
    this->_eventCcw = eventCcw;
    this->_eventPressed = eventPressed;
    this->_eventPressedLong = eventPressedLong;

    bool isRAK = false;
#ifdef RAK_4631
    isRAK = true;
#endif

    if (!isRAK || pinPress != 0) {
        pinMode(pinPress, INPUT_PULLUP);
        attachInterrupt(pinPress, onIntPress, RISING);
    }
    if (!isRAK || this->_pinA != 0) {
        pinMode(this->_pinA, INPUT_PULLUP);
        attachInterrupt(this->_pinA, onIntA, CHANGE);
    }
    if (!isRAK || this->_pinA != 0) {
        pinMode(this->_pinB, INPUT_PULLUP);
        attachInterrupt(this->_pinB, onIntB, CHANGE);
    }

    this->rotaryLevelA = digitalRead(this->_pinA);
    this->rotaryLevelB = digitalRead(this->_pinB);
    LOG_INFO("Rotary initialized (%d, %d, %d)", this->_pinA, this->_pinB, pinPress);
}

int32_t RotaryEncoderInterruptBase::runOnce()
{
    InputEvent e;
    e.inputEvent = INPUT_BROKER_NONE;
    e.source = this->_originName;
    unsigned long now = millis();

    // Handle press long/short detection
    if (this->action == ROTARY_ACTION_PRESSED) {
        bool buttonPressed = !digitalRead(_pinPress);
        if (!pressDetected && buttonPressed) {
            pressDetected = true;
            pressStartTime = now;
        }

        if (pressDetected) {
            uint32_t duration = now - pressStartTime;
            if (!buttonPressed) {
                // released -> if short press, send short, else already sent long
                if (duration < LONG_PRESS_DURATION && now - lastPressKeyTime >= pressDebounceMs) {
                    lastPressKeyTime = now;
                    LOG_DEBUG("Rotary event Press short");
                    e.inputEvent = this->_eventPressed;
                }
                pressDetected = false;
                pressStartTime = 0;
                lastPressLongEventTime = 0;
                this->action = ROTARY_ACTION_NONE;
            } else if (duration >= LONG_PRESS_DURATION && this->_eventPressedLong != INPUT_BROKER_NONE &&
                       lastPressLongEventTime == 0) {
                // fire single-shot long press
                lastPressLongEventTime = now;
                LOG_DEBUG("Rotary event Press long");
                e.inputEvent = this->_eventPressedLong;
            }
        }
    } else if (this->action == ROTARY_ACTION_CW) {
        LOG_DEBUG("Rotary event CW");
        e.inputEvent = this->_eventCw;
    } else if (this->action == ROTARY_ACTION_CCW) {
        LOG_DEBUG("Rotary event CCW");
        e.inputEvent = this->_eventCcw;
    }

    if (e.inputEvent != INPUT_BROKER_NONE) {
        this->notifyObservers(&e);
    }

    if (!pressDetected) {
        this->action = ROTARY_ACTION_NONE;
    }

    return INT32_MAX;
}

void RotaryEncoderInterruptBase::intPressHandler()
{
    this->action = ROTARY_ACTION_PRESSED;
    setIntervalFromNow(20); // start checking for long/short
}

void RotaryEncoderInterruptBase::intAHandler()
{
    // CW rotation (at least on most common rotary encoders)
    int currentLevelA = digitalRead(this->_pinA);
    if (this->rotaryLevelA == currentLevelA) {
        return;
    }
    this->rotaryLevelA = currentLevelA;
    this->rotaryStateCCW = intHandler(currentLevelA == HIGH, this->rotaryLevelB, ROTARY_ACTION_CCW, this->rotaryStateCCW);
}

void RotaryEncoderInterruptBase::intBHandler()
{
    // CW rotation (at least on most common rotary encoders)
    int currentLevelB = digitalRead(this->_pinB);
    if (this->rotaryLevelB == currentLevelB) {
        return;
    }
    this->rotaryLevelB = currentLevelB;
    this->rotaryStateCW = intHandler(currentLevelB == HIGH, this->rotaryLevelA, ROTARY_ACTION_CW, this->rotaryStateCW);
}

/**
 * @brief Rotary action implementation.
 *   We assume, the following pin setup:
 *    A   --||
 *    GND --||]========
 *    B   --||
 *
 * @return The new state for rotary pin.
 */
RotaryEncoderInterruptBaseStateType RotaryEncoderInterruptBase::intHandler(bool actualPinRaising, int otherPinLevel,
                                                                           RotaryEncoderInterruptBaseActionType action,
                                                                           RotaryEncoderInterruptBaseStateType state)
{
    RotaryEncoderInterruptBaseStateType newState = state;
    if (actualPinRaising && (otherPinLevel == LOW)) {
        if (state == ROTARY_EVENT_CLEARED) {
            newState = ROTARY_EVENT_OCCURRED;
            if ((this->action != ROTARY_ACTION_PRESSED) && (this->action != action)) {
                this->action = action;
            }
        }
    } else if (!actualPinRaising && (otherPinLevel == HIGH)) {
        // Logic to prevent bouncing.
        newState = ROTARY_EVENT_CLEARED;
    }
    setIntervalFromNow(50); // TODO: this modifies a non-volatile variable!

    return newState;
}
