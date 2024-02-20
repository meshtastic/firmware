#include "RotaryEncoderInterruptBase.h"
#include "configuration.h"

RotaryEncoderInterruptBase::RotaryEncoderInterruptBase(const char *name) : concurrency::OSThread(name)
{
    this->_originName = name;
}

void RotaryEncoderInterruptBase::init(
    uint8_t pinA, uint8_t pinB, uint8_t pinPress, char eventCw, char eventCcw, char eventPressed,
    //    std::function<void(void)> onIntA, std::function<void(void)> onIntB, std::function<void(void)> onIntPress) :
    void (*onIntA)(), void (*onIntB)(), void (*onIntPress)())
{
    this->_pinA = pinA;
    this->_pinB = pinB;
    this->_eventCw = eventCw;
    this->_eventCcw = eventCcw;
    this->_eventPressed = eventPressed;

    pinMode(pinPress, INPUT_PULLUP);
    pinMode(this->_pinA, INPUT_PULLUP);
    pinMode(this->_pinB, INPUT_PULLUP);

    //    attachInterrupt(pinPress, onIntPress, RISING);
    attachInterrupt(pinPress, onIntPress, RISING);
    attachInterrupt(this->_pinA, onIntA, CHANGE);
    attachInterrupt(this->_pinB, onIntB, CHANGE);

    this->rotaryLevelA = digitalRead(this->_pinA);
    this->rotaryLevelB = digitalRead(this->_pinB);
    LOG_INFO("Rotary initialized (%d, %d, %d)\n", this->_pinA, this->_pinB, pinPress);
}

int32_t RotaryEncoderInterruptBase::runOnce()
{
    InputEvent e;
    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
    e.source = this->_originName;

    if (this->action == ROTARY_ACTION_PRESSED) {
        LOG_DEBUG("Rotary event Press\n");
        e.inputEvent = this->_eventPressed;
    } else if (this->action == ROTARY_ACTION_CW) {
        LOG_DEBUG("Rotary event CW\n");
        e.inputEvent = this->_eventCw;
    } else if (this->action == ROTARY_ACTION_CCW) {
        LOG_DEBUG("Rotary event CCW\n");
        e.inputEvent = this->_eventCcw;
    }

    if (e.inputEvent != meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE) {
        this->notifyObservers(&e);
    }

    this->action = ROTARY_ACTION_NONE;

    return INT32_MAX;
}

void RotaryEncoderInterruptBase::intPressHandler()
{
    this->action = ROTARY_ACTION_PRESSED;
    setIntervalFromNow(20); // TODO: this modifies a non-volatile variable!
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
