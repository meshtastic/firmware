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
        setInterval(INT32_MAX);
        enabled = false;
        return;
    }

    inputBroker->registerSource(this);
}
