#include "facesKbI2cImpl.h"
#include "InputBroker.h"

FacesKbI2cImpl *facesKbI2cImpl;

FacesKbI2cImpl::FacesKbI2cImpl() :
    KbI2cBase("facesKB")
{
}

void FacesKbI2cImpl::init()
{
    if (faceskb_found != FACESKB_ADDR)
    {
        // Input device is not detected.
        return;
    }

    inputBroker->registerSource(this);
}
