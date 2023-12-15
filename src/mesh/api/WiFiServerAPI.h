#pragma once

#include "ServerAPI.h"
#include <WiFi.h>

/**
 * Provides both debug printing and, if the client starts sending protobufs to us, switches to send/receive protobufs
 * (and starts dropping debug printing - FIXME, eventually those prints should be encapsulated in protobufs).
 */
class WiFiServerAPI : public ServerAPI<WiFiClient>
{
  public:
    explicit WiFiServerAPI(WiFiClient &_client);
};

/**
 * Listens for incoming connections and does accepts and creates instances of WiFiServerAPI as needed
 */
class WiFiServerPort : public APIServerPort<WiFiServerAPI, WiFiServer>
{
  public:
    explicit WiFiServerPort(int port);
};

void initApiServer(int port = 4403);
void deInitApiServer();