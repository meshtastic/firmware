#include "UpDownInterruptBase.h"
#include "configuration.h"

UpDownInterruptBase::UpDownInterruptBase(const char *name)
{
    this->_originName = name;
}

void UpDownInterruptBase::init(uint8_t pinDown, uint8_t pinUp, uint8_t pinPress, char eventDown, char eventUp, char eventPressed,
                               void (*onIntDown)(), void (*onIntUp)(), void (*onIntPress)())
{
    this->_pinDown = pinDown;
    this->_pinUp = pinUp;
    this->_eventDown = eventDown;
    this->_eventUp = eventUp;
    this->_eventPressed = eventPressed;

    pinMode(pinPress, INPUT_PULLUP);
    pinMode(this->_pinDown, INPUT_PULLUP);
    pinMode(this->_pinUp, INPUT_PULLUP);

    attachInterrupt(pinPress, onIntPress, RISING);
    attachInterrupt(this->_pinDown, onIntDown, RISING);
    attachInterrupt(this->_pinUp, onIntUp, RISING);

    LOG_DEBUG("Up/down/press GPIO initialized (%d, %d, %d)\n", this->_pinUp, this->_pinDown, pinPress);
}

void UpDownInterruptBase::intPressHandler()
{
    InputEvent e;
    e.source = this->_originName;
    LOG_DEBUG("GPIO event Press\n");
    e.inputEvent = this->_eventPressed;
    this->notifyObservers(&e);
}

void UpDownInterruptBase::intDownHandler()
{
    InputEvent e;
    e.source = this->_originName;
    LOG_DEBUG("GPIO event Down\n");
    e.inputEvent = this->_eventDown;
    this->notifyObservers(&e);
}

void UpDownInterruptBase::intUpHandler()
{
    InputEvent e;
    e.source = this->_originName;
    LOG_DEBUG("GPIO event Up\n");
    e.inputEvent = this->_eventUp;
    this->notifyObservers(&e);
}
