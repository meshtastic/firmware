#include "configuration.h"

#include <Arduino.h>

#include "ethServerAPI.h"

#if HAS_ETHERNET

static ethServerPort *apiPort = nullptr;

void initEthApiServer(int port)
{
    if (!apiPort) {
        apiPort = new ethServerPort(port);
        LOG_INFO("Ethernet API server listening on TCP port %d", port);
        apiPort->init();
    }
}

void deInitEthApiServer()
{
    if (apiPort) {
        LOG_INFO("Deinit Ethernet API server");
        delete apiPort;
        apiPort = nullptr;
    }
}

#if !HAS_WIFI
void initApiServer(int port)
{
    initEthApiServer(port);
}
void deInitApiServer()
{
    deInitEthApiServer();
}
#endif

ethServerAPI::ethServerAPI(MeshEthernetClient &_client) : ServerAPI(_client)
{
    LOG_INFO("Incoming ethernet connection");
    api_type = TYPE_ETH;
}

ethServerPort::ethServerPort(int port) : APIServerPort(port) {}

#endif