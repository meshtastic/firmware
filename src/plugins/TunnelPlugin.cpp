#include "TunnelPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <Arduino.h>
#include "main.h"
#include <assert.h>

#define RXD2 16
#define TXD2 17
#define SERIALPLUGIN_RX_BUFFER 128
#define SERIALPLUGIN_STRING_MAX Constants_DATA_PAYLOAD_LEN
#define SERIALPLUGIN_TIMEOUT 250
#define SERIALPLUGIN_BAUD 38400
#define SERIALPLUGIN_ACK 1

TunnelPlugin *tunnelPlugin;

TunnelPlugin::TunnelPlugin() : concurrency::OSThread("TunnelPlugin") {}

char tunnelSerialStringChar[Constants_DATA_PAYLOAD_LEN];

int32_t TunnelPlugin::runOnce()
{
#ifndef NO_ESP32

    radioConfig.preferences.tunnelplugin_enabled = 1;
    radioConfig.preferences.tunnelplugin_echo_enabled = 1;

    if (radioConfig.preferences.tunnelplugin_enabled) {

        if (firstTime) {
            DEBUG_MSG("Initializing tunnel serial peripheral interface\n");
            Serial2.begin(SERIALPLUGIN_BAUD, SERIAL_8N1, RXD2, TXD2);
            Serial2.setTimeout(SERIALPLUGIN_TIMEOUT);
            Serial2.setRxBufferSize(SERIALPLUGIN_RX_BUFFER);

            firstTime = 0;

        } else {
            String serialString;

            while (Serial2.available()) {
                serialString = Serial2.readString();
                serialString.toCharArray(tunnelSerialStringChar, Constants_DATA_PAYLOAD_LEN);
                Serial2.println(serialString);
                if(screen)
                    screen->print(serialString.c_str());
                DEBUG_MSG("Tunnel Reading Recevied: %s\n", tunnelSerialStringChar);
            }
        }
        return (10);
    } else {
        DEBUG_MSG("Tunnel Plugin Disabled\n");

        return (INT32_MAX);
    }
#else
    return INT32_MAX;`
#endif
}
