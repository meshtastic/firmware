#pragma once

#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_OTA)

#ifdef WIZNET_5500_EVB_PICO2
#include <Ethernet.h>
#else
#include <RAK13800_W5100S.h>
#endif

#define ETH_OTA_PORT 4243

/// Initialize the Ethernet OTA server (call after Ethernet is connected)
void initEthOTA();

/// Poll for incoming OTA connections (call periodically from ethClient
/// reconnect loop)
void ethOTALoop();

#endif // HAS_ETHERNET && HAS_ETHERNET_OTA
