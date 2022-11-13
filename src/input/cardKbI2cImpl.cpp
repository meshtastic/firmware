#include "cardKbI2cImpl.h"
#include "InputBroker.h"

#if HAS_WIRE

CardKbI2cImpl *cardKbI2cImpl;

CardKbI2cImpl::CardKbI2cImpl() :
    KbI2cBase("cardKB")
{
}

void CardKbI2cImpl::init()
{
    if (i2cScanMap[CARDKB_ADDR].addr != CARDKB_ADDR)
    {
        // Input device is not detected.
        return;
    }

    inputBroker->registerSource(this);
}

#endif