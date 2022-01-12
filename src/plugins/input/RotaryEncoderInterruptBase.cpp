#include "configuration.h"
#include "RotaryEncoderInterruptBase.h"

RotaryEncoderInterruptBase::RotaryEncoderInterruptBase(
    const char *name) :
    concurrency::OSThread(name)
{
    this->_originName = name;
}

void RotaryEncoderInterruptBase::init(
    uint8_t pinA, uint8_t pinB, uint8_t pinPress,
    char eventCw, char eventCcw, char eventPressed,
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
    DEBUG_MSG("Rotary initialized (%d, %d, %d)\n",
        this->_pinA, this->_pinB, pinPress);
}


int32_t RotaryEncoderInterruptBase::runOnce()
{
    InputEvent e;
    e.inputEvent = InputEventChar_NULL;
    e.origin = this->_originName;

    if (this->action == ROTARY_ACTION_PRESSED)
    {
        DEBUG_MSG("Rotary event Press\n");
        e.inputEvent = this->_eventPressed;
    }
    else if (this->action == ROTARY_ACTION_CW)
    {
        DEBUG_MSG("Rotary event CW\n");
        e.inputEvent = this->_eventCw;
    }
    else if (this->action == ROTARY_ACTION_CCW)
    {
        DEBUG_MSG("Rotary event CW\n");
        e.inputEvent = this->_eventCcw;
    }

    if (e.inputEvent != InputEventChar_NULL)
    {
        this->notifyObservers(&e);
    }

    this->action = ROTARY_ACTION_NONE;

    return 30000;
}


void RotaryEncoderInterruptBase::intPressHandler()
{
    this->action = ROTARY_ACTION_PRESSED;
    runned(millis());
    setInterval(20);
}

/**
 * @brief Rotary action implementation.
 *   We assume, the following pin setup:
 *    A   --||
 *    GND --||]======== 
 *    B   --||
 * 
 * @return The new level of the actual pin (that is actualPinCurrentLevel).
 */
void RotaryEncoderInterruptBase::intAHandler()
{
    // CW rotation (at least on most common rotary encoders)
    int currentLevelA = digitalRead(this->_pinA);
    if (this->rotaryLevelA == currentLevelA)
    {
        return;
    }
    this->rotaryLevelA = currentLevelA;
    bool pinARaising = currentLevelA == HIGH;
    if (pinARaising && (this->rotaryLevelB == LOW))
    {
        if (this->rotaryStateCCW == ROTARY_EVENT_CLEARED)
        {
            this->rotaryStateCCW = ROTARY_EVENT_OCCURRED;
            if ((this->action == ROTARY_ACTION_NONE)
                || (this->action == ROTARY_ACTION_CW))
            {
                this->action = ROTARY_ACTION_CCW;
                DEBUG_MSG("Rotary action CCW\n");
            }
        }
    }
    else if (!pinARaising && (this->rotaryLevelB == HIGH))
    {
        // Logic to prevent bouncing.
        this->rotaryStateCCW = ROTARY_EVENT_CLEARED;
    }
    runned(millis());
    setInterval(50);
}

void RotaryEncoderInterruptBase::intBHandler()
{
    // CW rotation (at least on most common rotary encoders)
    int currentLevelB = digitalRead(this->_pinB);
    if (this->rotaryLevelB == currentLevelB)
    {
        return;
    }
    this->rotaryLevelB = currentLevelB;
    bool pinBRaising = currentLevelB == HIGH;
    if (pinBRaising && (this->rotaryLevelA == LOW))
    {
        if (this->rotaryStateCW == ROTARY_EVENT_CLEARED)
        {
            this->rotaryStateCW = ROTARY_EVENT_OCCURRED;
            if ((this->action == ROTARY_ACTION_NONE)
                || (this->action == ROTARY_ACTION_CCW))
            {
                this->action = ROTARY_ACTION_CW;
                DEBUG_MSG("Rotary action CW\n");
            }
        }
    }
    else if (!pinBRaising && (this->rotaryLevelA == HIGH))
    {
        // Logic to prevent bouncing.
        this->rotaryStateCW = ROTARY_EVENT_CLEARED;
    }
    runned(millis());
    setInterval(50);
}
