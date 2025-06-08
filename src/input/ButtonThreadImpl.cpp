#include "ButtonThreadImpl.h"
#include "InputBroker.h"
#include "configuration.h"

ButtonThreadImpl *aButtonThreadImpl;

ButtonThreadImpl::ButtonThreadImpl() : ButtonThread("UserButton") {}

void ButtonThreadImpl::init() // init should give the pin number and the action to perform and whether to do the legacy actions
{
    if (inputBroker)
        inputBroker->registerSource(this);
}
