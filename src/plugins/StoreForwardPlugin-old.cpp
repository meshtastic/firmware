#if 0

#include "StoreForwardPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <Arduino.h>

StoreForwardPlugin *storeForwardPlugin;
StoreForwardPluginRadio *storeForwardPluginRadio;

StoreForwardPlugin::StoreForwardPlugin() : concurrency::OSThread("StoreForwardPlugin") {}

int32_t StoreForwardPlugin::runOnce()
{
#if 0

#ifndef NO_ESP32

    /*
        Uncomment the preferences below if you want to use the plugin
        without having to configure it from the PythonAPI or WebUI.
    */

    radioConfig.preferences.store_forward_plugin_enabled = 0;

    if (radioConfig.preferences.store_forward_plugin_enabled) {

        if (firstTime) {

            DEBUG_MSG("Initializing Store & Forward Plugin\n");
            /*
             */

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
            // storeForwardPluginRadio = new StoreForwardPluginRadio();

            firstTime = 0;

        } else {
            // TBD
        }

        return (1000);
    } else {
        DEBUG_MSG("Store & Forward Plugin - Disabled\n");

        return (INT32_MAX);
    }

#endif
#endif
    return (INT32_MAX);
}

MeshPacket *StoreForwardPluginRadio::allocReply()
{

    auto reply = allocDataPacket(); // Allocate a packet for sending

    return reply;
}

void StoreForwardPluginRadio::sendPayload(NodeNum dest, bool wantReplies)
{
#if 0
    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;

    service.sendToMesh(p);
#endif
}

bool StoreForwardPluginRadio::handleReceived(const MeshPacket &mp)
{

#if 0
#ifndef NO_ESP32

    if (radioConfig.preferences.store_forward_plugin_enabled) {

        // auto &p = mp.decoded.data;

        if (mp.from != nodeDB.getNodeNum()) {
            DEBUG_MSG("Store & Forward Plugin -- Print Start ---------- ---------- ---------- ---------- ----------\n");
            printPacket("PACKET FROM RADIO", &mp);
            DEBUG_MSG("Store & Forward Plugin -- Print End ---------- ---------- ---------- ---------- ----------\n");
        }

    } else {
        DEBUG_MSG("Store & Forward Plugin - Disabled\n");
    }

#endif
#endif

    return true; // Let others look at this message also if they want
}

#endif