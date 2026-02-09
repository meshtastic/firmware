#include "configuration.h"
#include <Arduino.h>
#include "ethServerAPI.h"

#if HAS_ETHERNET
    #if !HAS_WIFI
        static ethServerPort *apiPort;

        void initApiServer(int port)
        {
            if (!apiPort) {
                apiPort = new ethServerPort(port);
                LOG_INFO("API server listening on TCP port %d", port);
                apiPort->init();
            }
        }
    #endif

    ethServerAPI::ethServerAPI(MeshEthernetClient &_client) : ServerAPI(_client)
    {
        LOG_INFO("Incoming ethernet connection");
        api_type = TYPE_ETH;
    }

    ethServerPort::ethServerPort(int port) : APIServerPort(port) {}

#endif