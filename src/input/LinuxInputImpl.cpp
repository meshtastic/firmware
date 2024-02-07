#include "configuration.h"
#if ARCH_PORTDUINO
#include "InputBroker.h"
#include "LinuxInputImpl.h"

LinuxInputImpl *aLinuxInputImpl;

LinuxInputImpl::LinuxInputImpl() : LinuxInput("LinuxInput") {}

void LinuxInputImpl::init()
{
    inputBroker->registerSource(this);
}

#endif