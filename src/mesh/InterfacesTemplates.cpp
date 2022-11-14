#include "SX126xInterface.h"
#include "SX126xInterface.cpp"
#include "SX128xInterface.h"
#include "SX128xInterface.cpp"

// We need this declaration for proper linking in derived classes
template class SX126xInterface<SX1262>;
template class SX126xInterface<SX1268>;
template class SX126xInterface<LLCC68>;

#if defined(RADIOLIB_GODMODE)
template class SX128xInterface<SX1280>;
#endif