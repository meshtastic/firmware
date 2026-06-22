#pragma once

#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_TLS_API) && defined(ARCH_RP2040)

// HTTPS server on TCP/443 that reuses the HTTP API handlers via the
// transport-agnostic IStreamReadWrite interface (see ethApiHandlers.h). The
// server waits for the cert produced by [[ethCert.h]] before binding the
// listening socket, so it is safe to call this from setup() / reconnectETH().
//
// Idempotent: subsequent calls are no-ops.
void initEthTlsApiServer();

#endif // HAS_ETHERNET && HAS_ETHERNET_TLS_API && ARCH_RP2040
