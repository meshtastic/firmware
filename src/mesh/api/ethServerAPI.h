#pragma once
#include "ServerAPI.h"

#if HAS_ETHERNET
    #if defined(ESP32) && (defined(ETH_PHY_TYPE) || defined(USE_WS5500))
        #if defined(ETH_PHY_TYPE)
            #include <ETH.h>
        #else
            #include <ETHClass2.h>
        #endif
        typedef WiFiClient MeshEthernetClient;
        typedef WiFiServer MeshEthernetServer;
    #else
        #include <Ethernet.h>
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

    void initApiServer(int port);
#endif