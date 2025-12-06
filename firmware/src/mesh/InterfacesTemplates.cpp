#include "LR11x0Interface.cpp"
#include "LR11x0Interface.h"
#include "SX126xInterface.cpp"
#include "SX126xInterface.h"
#include "SX128xInterface.cpp"
#include "SX128xInterface.h"
#include "api/ServerAPI.cpp"
#include "api/ServerAPI.h"

// We need this declaration for proper linking in derived classes
#if RADIOLIB_EXCLUDE_SX126X != 1
template class SX126xInterface<SX1262>;
template class SX126xInterface<SX1268>;
template class SX126xInterface<LLCC68>;
#endif
#if RADIOLIB_EXCLUDE_SX128X != 1
template class SX128xInterface<SX1280>;
#endif
#if RADIOLIB_EXCLUDE_LR11X0 != 1
template class LR11x0Interface<LR1110>;
template class LR11x0Interface<LR1120>;
template class LR11x0Interface<LR1121>;
#endif
#ifdef ARCH_STM32WL
template class SX126xInterface<STM32WLx>;
#endif

#if HAS_ETHERNET && !defined(USE_WS5500)
#include "api/ethServerAPI.h"
template class ServerAPI<EthernetClient>;
template class APIServerPort<ethServerAPI, EthernetServer>;
#endif

#if HAS_WIFI
#include "api/WiFiServerAPI.h"
template class ServerAPI<WiFiClient>;
template class APIServerPort<WiFiServerAPI, WiFiServer>;
#endif