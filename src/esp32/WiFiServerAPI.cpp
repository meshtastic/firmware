#include "WiFiServerAPI.h"
#include "PowerFSM.h"
#include "configuration.h"
#include <Arduino.h>

WiFiServerAPI::WiFiServerAPI(WiFiClient &_client) : StreamAPI(&client), client(_client)
{
    DEBUG_MSG("Incoming connection from %s\n", client.remoteIP().toString().c_str());
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

void WiFiServerAPI::loop()
{
    if (client.connected()) {
        StreamAPI::loop();
    } else {
        DEBUG_MSG("Client dropped connection, closing UDP server\n");
        delete this;
    }
}

#define MESHTASTIC_PORTNUM 4403

WiFiServerPort::WiFiServerPort() : WiFiServer(MESHTASTIC_PORTNUM) {}

void WiFiServerPort::init()
{
    DEBUG_MSG("Listening on TCP port %d\n", MESHTASTIC_PORTNUM);
    begin();
}

void WiFiServerPort::loop()
{
    auto client = available();
    if (client) {
        new WiFiServerAPI(client);
    }
}