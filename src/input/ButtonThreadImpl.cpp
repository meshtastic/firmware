#include "ButtonThreadImpl.h"
#include "InputBroker.h"
#include "configuration.h"

ButtonThreadImpl::ButtonThreadImpl(char *_name) : ButtonThread(_name) {}

void ButtonThreadImpl::init(uint8_t pinNumber, bool activeLow, bool activePullup, uint32_t pullupSense, voidFuncPtr intRoutine,
                            input_broker_event singlePress, input_broker_event longPress, input_broker_event doublePress,
                            input_broker_event triplePress, input_broker_event shortLong, bool touchQuirk)
{
    if (inputBroker)
        inputBroker->registerSource(this);
    initButton(pinNumber, activeLow, activePullup, pullupSense, intRoutine, singlePress, longPress, doublePress, triplePress,
               shortLong, touchQuirk);
}
