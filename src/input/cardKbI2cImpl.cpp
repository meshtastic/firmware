#include "cardKbI2cImpl.h"
#include "InputBroker.h"
#include "main.h"

CardKbI2cImpl *cardKbI2cImpl;

CardKbI2cImpl::CardKbI2cImpl() : KbI2cBase("cardKB") {}

void CardKbI2cImpl::init()
{
    if (cardkb_found.address == 0x00) {
        disable();
        return;
    }

    inputBroker->registerSource(this);
}
