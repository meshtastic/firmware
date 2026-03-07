#include "configuration.h"
#include <Arduino.h>

#if HAS_ETHERNET && !defined(USE_WS5500)

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
    auto ip = _client.remoteIP();
    LOG_INFO("Incoming ethernet connection from %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
    api_type = TYPE_ETH;
}

ethServerPort::ethServerPort(int port) : APIServerPort(port) {}

#endif