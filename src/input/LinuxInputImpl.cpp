#if ARCH_RASPBERRY_PI
#include "LinuxInputImpl.h"
#include "InputBroker.h"

LinuxInputImpl *aLinuxInputImpl;

LinuxInputImpl::LinuxInputImpl() : LinuxInput("LinuxInput") {}

void LinuxInputImpl::init()
{
    inputBroker->registerSource(this);
}

#endif