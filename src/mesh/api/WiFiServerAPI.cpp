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
        LOG_INFO("API server listening on TCP port %d\n", port);
        apiPort->init();
    }
}
void deInitApiServer()
{
    delete apiPort;
}

WiFiServerAPI::WiFiServerAPI(WiFiClient &_client) : ServerAPI(_client)
{
    LOG_INFO("Incoming wifi connection\n");
}

WiFiServerPort::WiFiServerPort(int port) : APIServerPort(port) {}
#endif