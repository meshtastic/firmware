#pragma once

#include "configuration.h"

#if HAS_ETHERNET && defined(HAS_ETHERNET_OTA) && defined(HAS_ETHERNET_API)

#include "ethApiHandlers.h"

#define ETH_HTTP_OTA_PORT 4244

/// Initialize the legacy HTTP-only OTA server on TCP/4244. Kept for backwards
/// compatibility — the same OTA endpoints are also served by the unified
/// handleApiClient on port 80 (HTTP) and 443 (HTTPS) since the OTA handlers
/// were refactored onto IStreamReadWrite.
void initEthHttpOTA();

/// Poll for incoming connections on TCP/4244 (call periodically). Each
/// accepted client gets routed through handleApiClient — same code path
/// the TLS server uses.
void ethHttpOTALoop();

// Route-level OTA handlers. handleApiClient calls these for /api/v1/info,
// /api/v1/ota/nonce, /api/v1/ota — same flow on plain TCP and TLS.
void handleOtaInfo(IStreamReadWrite &client);
void handleOtaNonce(IStreamReadWrite &client);
void handleOtaUpload(IStreamReadWrite &client, const Request &req);
void handleOtaOptions(IStreamReadWrite &client, const char *methods);

#endif // HAS_ETHERNET && HAS_ETHERNET_OTA && HAS_ETHERNET_API
