#pragma once

#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_WEBSERVER

#define WEBSOCKET_API_PATH "/api/v1/ws"

namespace httpsserver
{
class HTTPServer;
class HTTPSServer;
} // namespace httpsserver

/// Serve the PhoneAPI at WEBSOCKET_API_PATH: one ToRadio or FromRadio protobuf per binary message.
void registerWebSocketAPI(httpsserver::HTTPServer *insecureServer, httpsserver::HTTPSServer *secureServer);

/// Push queued FromRadio messages to the attached client. Returns true while a client is attached.
bool serviceWebSocketAPI();

#endif
