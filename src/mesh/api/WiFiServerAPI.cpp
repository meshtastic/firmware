#include "configuration.h"
#include <Arduino.h>

#if HAS_WIFI
#include "WiFiServerAPI.h"

static WiFiServerPort *apiPort;

void initApiServer(int port)
{
    // Start API server on port 4403
    if (!apiPort) {
        apiPort = new WiFiServerPort(port);
        LOG_INFO("API server listen on TCP port %d", port);
        apiPort->init();
    }
}
void deInitApiServer()
{
    if (apiPort) {
        delete apiPort;
        apiPort = nullptr;
    }
}

WiFiServerAPI::WiFiServerAPI(WiFiClient &_client) : ServerAPI(_client)
{
    api_type = TYPE_WIFI;
    auto ip = _client.remoteIP();
    LOG_INFO("Incoming wifi connection from %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

WiFiServerPort::WiFiServerPort(int port) : APIServerPort(port) {}
#endif