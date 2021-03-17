#include "WiFiServerAPI.h"
#include "PowerFSM.h"
#include "configuration.h"
#include <Arduino.h>

static WiFiServerPort *apiPort;

void initApiServer()
{
    // Start API server on port 4403
    if (!apiPort) {
        apiPort = new WiFiServerPort();
        apiPort->init();
    }
}

WiFiServerAPI::WiFiServerAPI(WiFiClient &_client) : StreamAPI(&client), client(_client)
{
    DEBUG_MSG("Incoming wifi connection\n");
}

WiFiServerAPI::~WiFiServerAPI()
{
    client.stop();

    // FIXME - delete this if the client dropps the connection!
}

/// Hookable to find out when connection changes
void WiFiServerAPI::onConnectionChanged(bool connected)
{
    // FIXME - we really should be doing global reference counting to see if anyone is currently using serial or wifi and if so,
    // block sleep

    if (connected) { // To prevent user confusion, turn off bluetooth while using the serial port api
        powerFSM.trigger(EVENT_SERIAL_CONNECTED);
    } else {
        powerFSM.trigger(EVENT_SERIAL_DISCONNECTED);
    }
}

/// override close to also shutdown the TCP link
void WiFiServerAPI::close() {
    client.stop(); // drop tcp connection
    StreamAPI::close();
}

bool WiFiServerAPI::loop()
{
    if (client.connected()) {
        StreamAPI::loop();
        return true;
    } else {
        return false;       
    }
}

#define MESHTASTIC_PORTNUM 4403

WiFiServerPort::WiFiServerPort() : WiFiServer(MESHTASTIC_PORTNUM), concurrency::OSThread("ApiServer") {}

void WiFiServerPort::init()
{
    DEBUG_MSG("API server listening on TCP port %d\n", MESHTASTIC_PORTNUM);
    begin();
}

int32_t WiFiServerPort::runOnce()
{
    auto client = available();
    if (client) {
        // Close any previous connection (see FIXME in header file)
        if (openAPI) {
            DEBUG_MSG("Force closing previous TCP connection\n");
            delete openAPI;
        }

        openAPI = new WiFiServerAPI(client);
    }

    if (openAPI) {
        // Allow idle processing so the API can read from its incoming stream
        if(!openAPI->loop()) {
            // If our API link was up, shut it down

            DEBUG_MSG("Client dropped connection, closing API client\n");
            // Note: we can't call delete here because this object includes other state
            // besides the stream API.  Instead kill it later when we start a new instance
            delete openAPI;
            openAPI = NULL;
        }
        return 0; // run fast while our API server is running
    } else
        return 100; // only check occasionally for incoming connections
}