#include "SX126xInterface.cpp"
#include "SX126xInterface.h"
#include "SX128xInterface.cpp"
#include "SX128xInterface.h"

// We need this declaration for proper linking in derived classes
template class SX126xInterface<SX1262>;
template class SX126xInterface<SX1268>;
#ifdef ARCH_STM32WL
template class SX126xInterface<STM32WLx>;
#endif
template class SX126xInterface<LLCC68>;
template class SX128xInterface<SX1280>;
