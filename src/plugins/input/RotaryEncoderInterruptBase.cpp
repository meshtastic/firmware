#include "configuration.h"
#include "RotaryEncoderInterruptBase.h"

/*#define PIN_PUSH 21
#define PIN_A    22
#define PIN_B    23
*/

/*
RotaryEncoderInterruptBase *cannedMessagePlugin;

void IRAM_ATTR EXT_INT_PUSH()
{
  cannedMessagePlugin->pressed();
}

void IRAM_ATTR EXT_INT_DIRECTION_A()
{
  cannedMessagePlugin->directionA();
}

void IRAM_ATTR EXT_INT_DIRECTION_B()
{
  cannedMessagePlugin->directionB();
}
*/

RotaryEncoderInterruptBase::RotaryEncoderInterruptBase(
    uint8_t pinA, uint8_t pinB, uint8_t pinPress,
    char eventCw, char eventCcw, char eventPressed,
//        std::function<void(void)> onIntA, std::function<void(void)> onIntB, std::function<void(void)> onIntPress) :
        void (*onIntA)(), void (*onIntB)(), void (*onIntPress)()) :
    SinglePortPlugin("rotaryi", PortNum_TEXT_MESSAGE_APP),
    concurrency::OSThread("RotaryEncoderInterruptBase")
{
    this->_pinA = pinA;
    this->_pinB = pinB;
    this->_eventCw = eventCw;
    this->_eventCcw = eventCcw;
    this->_eventPressed = eventPressed;

    // TODO: make pins configurable
    pinMode(pinPress, INPUT_PULLUP);
    pinMode(this->_pinA, INPUT_PULLUP);
    pinMode(this->_pinB, INPUT_PULLUP);
//    attachInterrupt(pinPress, onIntPress, RISING);
    attachInterrupt(pinPress, onIntPress, RISING);
    attachInterrupt(this->_pinA, onIntA, CHANGE);
    attachInterrupt(this->_pinB, onIntB, CHANGE);
    this->rotaryLevelA = digitalRead(this->_pinA);
    this->rotaryLevelB = digitalRead(this->_pinB);
}

int32_t RotaryEncoderInterruptBase::runOnce()
{
    if (this->action == ACTION_PRESSED)
    {
        InputEvent e;
        e.inputEvent = INPUT_EVENT_SELECT;
        this->notifyObservers(&e);
    }
    return 30000;
}


void RotaryEncoderInterruptBase::intPressHandler()
{
    this->action = ACTION_PRESSED;
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
        if (this->rotaryStateCCW == EVENT_CLEARED)
        {
            this->rotaryStateCCW = EVENT_OCCURRED;
            if ((this->action == ACTION_NONE)
                || (this->action == ACTION_CCW))
            {
                this->action = ACTION_CW;
            }
        }
    }
    else if (!pinARaising && (this->rotaryLevelB == HIGH))
    {
        // Logic to prevent bouncing.
        this->rotaryStateCCW = EVENT_CLEARED;
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
        if (this->rotaryStateCW == EVENT_CLEARED)
        {
            this->rotaryStateCW = EVENT_OCCURRED;
            if ((this->action == ACTION_NONE)
                || (this->action == ACTION_CCW))
            {
                this->action = ACTION_CW;
            }
        }
    }
    else if (!pinBRaising && (this->rotaryLevelA == HIGH))
    {
        // Logic to prevent bouncing.
        this->rotaryStateCW = EVENT_CLEARED;
    }
    runned(millis());
    setInterval(50);
}
