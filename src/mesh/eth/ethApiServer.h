#pragma once

#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_API)

#define ETH_API_PORT 80

/// Initialize the Ethernet HTTP API server (call after Ethernet is connected)
void initEthApiServer();

/// Poll for incoming HTTP API connections (call periodically from ethClient loop)
void ethApiServerLoop();

#endif // HAS_ETHERNET && HAS_ETHERNET_API
