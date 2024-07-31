#include "configuration.h"
#include "InputBroker.h"
#include "SerialKeyboardImpl.h"

#ifdef INPUTBROKER_SERIAL_TYPE

SerialKeyboardImpl *aSerialKeyboardImpl;

SerialKeyboardImpl::SerialKeyboardImpl() : SerialKeyboard("serialKB") {}

void SerialKeyboardImpl::init()
{
    if (!INPUTBROKER_SERIAL_TYPE) {
        disable();
        return;
    }

    inputBroker->registerSource(this);
}

#endif // INPUTBROKER_SERIAL_TYPE