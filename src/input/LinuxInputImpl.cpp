#include "LinuxInputImpl.h"
#include "InputBroker.h"

#if ARCH_RASPBERRY_PI

LinuxInputImpl *aLinuxInputImpl;

LinuxInputImpl::LinuxInputImpl() : LinuxInput("LinuxInput") {}

void LinuxInputImpl::init()
{
    inputBroker->registerSource(this);
}

#endif