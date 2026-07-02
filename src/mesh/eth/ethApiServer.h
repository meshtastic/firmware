#pragma once

#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_API)

#define ETH_API_PORT 80

/// Initialize the Ethernet HTTP API server (call after Ethernet is connected).
/// Spawns an internal OSThread that polls accept() on a sub-second cadence,
/// independent of the 5s Ethernet client periodic - needed because the web
/// client makes many small back-to-back requests.
void initEthApiServer();

/// Drop the HTTP listener (keeps the worker) so a later initEthApiServer()
/// rebinds TCP/80. Called by reconnectETH() after a W5500 chip reset, whose
/// register wipe leaves the old socket dead.
void deInitEthApiServer();

#endif // HAS_ETHERNET && HAS_ETHERNET_API
