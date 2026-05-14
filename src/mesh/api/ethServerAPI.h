#pragma once

#include "configuration.h"

#include "ServerAPI.h"

#if HAS_ETHERNET && !defined(USE_WS5500) && !defined(USE_CH390D)
#if defined(ESP32) && defined(ETH_PHY_TYPE)
#include <ETH.h>
#include <WiFi.h>
typedef WiFiClient MeshEthernetClient;
typedef WiFiServer MeshEthernetServer;
#else
#include <RAK13800_W5100S.h>
typedef EthernetClient MeshEthernetClient;
typedef EthernetServer MeshEthernetServer;
#endif

/**
 * Provides both debug printing and, if the client starts sending protobufs to us, switches to send/receive protobufs
 * (and starts dropping debug printing - FIXME, eventually those prints should be encapsulated in protobufs).
 */
class ethServerAPI : public ServerAPI<MeshEthernetClient>
{
  public:
    explicit ethServerAPI(MeshEthernetClient &_client);
};

/**
 * Listens for incoming connections and does accepts and creates instances of EthernetServerAPI as needed
 */
class ethServerPort : public APIServerPort<ethServerAPI, MeshEthernetServer>
{
  public:
    explicit ethServerPort(int port);
};

void initEthApiServer(int port = SERVER_API_DEFAULT_PORT);
void deInitEthApiServer();

#if !HAS_WIFI
void initApiServer(int port = SERVER_API_DEFAULT_PORT);
void deInitApiServer();
#endif
#endif
