#include "configuration.h"
#include <Arduino.h>

#if HAS_ETHERNET

#include "ethServerAPI.h"

static ethServerPort *apiPort;

void initApiServer(int port)
{
    // Start API server on port 4403
    if (!apiPort) {
        apiPort = new ethServerPort(port);
        LOG_INFO("API server listening on TCP port %d", port);
        apiPort->init();
    }
}

ethServerAPI::ethServerAPI(EthernetClient &_client) : ServerAPI(_client)
{
    LOG_INFO("Incoming ethernet connection");
}

ethServerPort::ethServerPort(int port) : APIServerPort(port) {}

#endif