#include "cardKbI2cImpl.h"
#include "InputBroker.h"

CardKbI2cImpl *cardKbI2cImpl;

CardKbI2cImpl::CardKbI2cImpl() :
    KbI2cBase("cardKB")
{
}

void CardKbI2cImpl::init()
{
    if (cardkb_found != CARDKB_ADDR)
    {
        // Input device is not detected.
        return;
    }

    DEBUG_MSG("registerSource\n");
    inputBroker->registerSource(this);
}
