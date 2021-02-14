#include "StoreForwardPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <Arduino.h>

#include <assert.h>

#define STORE_RECORDS 5000
#define BYTES_PER_RECORDS 512

#define STOREFORWARDPLUGIN_ENABLED 0

StoreForwardPlugin *storeForwardPlugin;
StoreForwardPluginRadio *storeForwardPluginRadio;

StoreForwardPlugin::StoreForwardPlugin() : concurrency::OSThread("SerialPlugin") {}

// char serialStringChar[Constants_DATA_PAYLOAD_LEN];

int32_t StoreForwardPlugin::runOnce()
{
#ifndef NO_ESP32

    /*
        Uncomment the preferences below if you want to use the plugin
        without having to configure it from the PythonAPI or WebUI.
    */

    // radioConfig.preferences.store_forward_plugin_enabled = 1;
    // radioConfig.preferences.store_forward_plugin_records = 80;

    if (radioConfig.preferences.store_forward_plugin_enabled) {

        if (firstTime) {

            // Interface with the serial peripheral from in here.
            DEBUG_MSG("Initializing Store & Forward Plugin\n");

            // Router
            if (radioConfig.preferences.is_router) {
                if (ESP.getPsramSize()) {
                    if (ESP.getFreePsram() <= 1024 * 1024) {
                        // Do the startup here

                    } else {
                        DEBUG_MSG("Device has less than 1M of PSRAM free. Aborting startup.\n");
                        DEBUG_MSG("Store & Forward Plugin - Aborting Startup.\n");

                        return (INT32_MAX);
                    }

                } else {
                    DEBUG_MSG("Device doesn't have PSRAM.\n");
                    DEBUG_MSG("Store & Forward Plugin - Aborting Startup.\n");

                    return (INT32_MAX);
                }

                // Non-Router
            } else {
            }

            storeForwardPluginRadio = new StoreForwardPluginRadio();

            firstTime = 0;

        } else {
            // TBD
        }

        return (10);
    } else {
        DEBUG_MSG("Store & Forward Plugin - Disabled\n");

        return (INT32_MAX);
    }

#endif
}

MeshPacket *StoreForwardPluginRadio::allocReply()
{

    auto reply = allocDataPacket(); // Allocate a packet for sending

    return reply;
}

void StoreForwardPluginRadio::sendPayload(NodeNum dest, bool wantReplies)
{
    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;

    // p->want_ack = SERIALPLUGIN_ACK;

    // p->decoded.data.payload.size = strlen(serialStringChar); // You must specify how many bytes are in the reply
    // memcpy(p->decoded.data.payload.bytes, serialStringChar, p->decoded.data.payload.size);

    service.sendToMesh(p);
}

bool StoreForwardPluginRadio::handleReceived(const MeshPacket &mp)
{
#ifndef NO_ESP32

    if (STOREFORWARDPLUGIN_ENABLED) {

        // auto &p = mp.decoded.data;

        if (mp.from != nodeDB.getNodeNum()) {
            DEBUG_MSG("Store & Forward Plugin ---------- ---------- ---------- ---------- ----------\n");
            printPacket("PACKET FROM PHONE", &mp);
        }

    } else {
        DEBUG_MSG("Store & Forward Plugin - Disabled\n");
    }

#endif

    return true; // Let others look at this message also if they want
}
