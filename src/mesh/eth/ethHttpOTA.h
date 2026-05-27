#pragma once

#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_OTA)

#define ETH_HTTP_OTA_PORT 4244

/// Initialize the Ethernet HTTP OTA server (call after Ethernet is connected)
void initEthHttpOTA();

/// Poll for incoming HTTP OTA connections (call periodically from ethClient loop)
void ethHttpOTALoop();

#endif // HAS_ETHERNET && HAS_ETHERNET_OTA
