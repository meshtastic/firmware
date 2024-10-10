#include "UpDownInterruptBase.h"
#include "configuration.h"

UpDownInterruptBase::UpDownInterruptBase(const char *name) : concurrency::OSThread(name)
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

    LOG_DEBUG("Up/down/press GPIO initialized (%d, %d, %d)", this->_pinUp, this->_pinDown, pinPress);

    this->setInterval(100);
}

int32_t UpDownInterruptBase::runOnce()
{
    InputEvent e;
    e.inputEvent = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;

    if (this->action == UPDOWN_ACTION_PRESSED) {
        LOG_DEBUG("GPIO event Press");
        e.inputEvent = this->_eventPressed;
    } else if (this->action == UPDOWN_ACTION_UP) {
        LOG_DEBUG("GPIO event Up");
        e.inputEvent = this->_eventUp;
    } else if (this->action == UPDOWN_ACTION_DOWN) {
        LOG_DEBUG("GPIO event Down");
        e.inputEvent = this->_eventDown;
    }

    if (e.inputEvent != meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE) {
        e.source = this->_originName;
        e.kbchar = meshtastic_ModuleConfig_CannedMessageConfig_InputEventChar_NONE;
        this->notifyObservers(&e);
    }

    this->action = UPDOWN_ACTION_NONE;

    return 100;
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
