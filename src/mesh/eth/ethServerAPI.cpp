#include "ethServerAPI.h"
#include "configuration.h"
#include <Arduino.h>

static ethServerPort *apiPort;

void initApiServer(int port)
{
    // Start API server on port 4403
    if (!apiPort) {
        apiPort = new ethServerPort(port);
        DEBUG_MSG("API server listening on TCP port %d\n", port);
        apiPort->init();
    }
}

ethServerAPI::ethServerAPI(EthernetClient &_client) : StreamAPI(&client), client(_client)
{
    DEBUG_MSG("Incoming ethernet connection\n");
}

ethServerAPI::~ethServerAPI()
{
    client.stop();

    // FIXME - delete this if the client dropps the connection!
}

/// override close to also shutdown the TCP link
void ethServerAPI::close()
{
    client.stop(); // drop tcp connection
    StreamAPI::close();
}

/// Check the current underlying physical link to see if the client is currently connected
bool ethServerAPI::checkIsConnected()
{
    return client.connected();
}

int32_t ethServerAPI::runOnce()
{
    if (client.connected()) {
        return StreamAPI::runOnce();
    } else {
        DEBUG_MSG("Client dropped connection, suspending API service\n");
        enabled = false; // we no longer need to run
        return 0;
    }
}

/// If an api server is running, we try to spit out debug 'serial' characters there
void ethServerPort::debugOut(char c)
{
    if (apiPort && apiPort->openAPI)
        apiPort->openAPI->debugOut(c);
}


ethServerPort::ethServerPort(int port) : EthernetServer(port), concurrency::OSThread("ApiServer") {}

void ethServerPort::init()
{
    begin();
}

int32_t ethServerPort::runOnce()
{
    auto client = available();
    if (client) {
        // Close any previous connection (see FIXME in header file)
        if (openAPI) {
            DEBUG_MSG("Force closing previous TCP connection\n");
            delete openAPI;
        }

        openAPI = new ethServerAPI(client);
    }

    return 100; // only check occasionally for incoming connections
}
