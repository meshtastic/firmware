#include "configuration.h"
#include <Arduino.h>

#if HAS_WIFI
#include "WiFiServerAPI.h"

static std::unique_ptr<WiFiServerPort> apiPort;

void initApiServer(int port)
{
    // Start API server on port 4403
    if (!apiPort) {
        apiPort = std::make_unique<WiFiServerPort>(port);
        LOG_INFO("API server listen on TCP port %d", port);
        apiPort->init();
    }
}
void deInitApiServer()
{
    apiPort.reset();
}

WiFiServerAPI::WiFiServerAPI(WiFiClient &_client) : ServerAPI(_client)
{
    api_type = TYPE_WIFI;
    LOG_INFO("Incoming wifi connection");
}

WiFiServerPort::WiFiServerPort(int port) : APIServerPort(port) {}
#endif