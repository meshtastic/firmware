#include "StoreForwardPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <Arduino.h>
#include <map>

StoreForwardPlugin *storeForwardPlugin;
StoreForwardPluginRadio *storeForwardPluginRadio;

StoreForwardPlugin::StoreForwardPlugin() : concurrency::OSThread("StoreForwardPlugin") {}

int32_t StoreForwardPlugin::runOnce()
{

#ifndef NO_ESP32

    /*
        Uncomment the preferences below if you want to use the plugin
        without having to configure it from the PythonAPI or WebUI.
    */

    // radioConfig.preferences.store_forward_plugin_enabled = 1;
    // radioConfig.preferences.is_router = 1;

    if (radioConfig.preferences.store_forward_plugin_enabled) {

        if (firstTime) {

            /*
             */

            if (radioConfig.preferences.is_router) {
                DEBUG_MSG("Initializing Store & Forward Plugin - Enabled\n");
                // Router
                if (ESP.getPsramSize()) {
                    if (ESP.getFreePsram() >= 1024 * 1024) {
                        // Do the startup here
                        storeForwardPluginRadio = new StoreForwardPluginRadio();

                        firstTime = 0;

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

            } else {
                DEBUG_MSG("Initializing Store & Forward Plugin - Enabled but is_router is not turned on.\n");
                DEBUG_MSG(
                    "Initializing Store & Forward Plugin - If you want to use this plugin, you must also turn on is_router.\n");
                // Non-Router

                return (30 * 1000);
            }

        } else {
            // What do we do if it's not our first time?

            // Maybe some cleanup functions?
        }

    } else {
        DEBUG_MSG("Store & Forward Plugin - Disabled\n");

        return (INT32_MAX);
    }

#endif
    return (INT32_MAX);
}

// We saw a node.
uint32_t StoreForwardPlugin::sawNode(uint32_t node)
{

    /*
    TODO: Move receivedRecord into the PSRAM

    TODO: Gracefully handle the case where we run out of records.
            Maybe replace the oldest record that hasn't been seen in a while and assume they won't be back.

    TODO: Implment this as a std::map for quicker lookups (maybe it doesn't matter?).
    */

    DEBUG_MSG("looking for node - %i\n", node);
    for (int i = 0; i < 50; i++) {
        DEBUG_MSG("Iterating through the seen nodes - %d %d %d\n", i, receivedRecord[i][0], receivedRecord[i][1]);
        // First time seeing that node.
        if (receivedRecord[i][0] == 0) {
            DEBUG_MSG("New node! Woohoo! Win!\n");
            receivedRecord[i][0] = node;
            receivedRecord[i][1] = millis();

            return receivedRecord[i][1];
        }

        // We've seen this node before.
        if (receivedRecord[i][0] == node) {
            DEBUG_MSG("We've seen this node before\n");
            uint32_t lastSaw = receivedRecord[i][1];
            receivedRecord[i][1] = millis();
            return lastSaw;
        }
    }

    return 0;
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

    service.sendToMesh(p);
}

bool StoreForwardPluginRadio::handleReceived(const MeshPacket &mp)
{
#ifndef NO_ESP32
    if (radioConfig.preferences.store_forward_plugin_enabled) {
        // auto &p = mp.decoded.data;

        if (mp.from != nodeDB.getNodeNum()) {
            DEBUG_MSG("Store & Forward Plugin -- Print Start ---------- ---------- ---------- ---------- ----------\n\n\n");
            printPacket("----- PACKET FROM RADIO", &mp);
            // DEBUG_MSG("\n\nStore & Forward Plugin -- Print End ---------- ---------- ---------- ---------- ----------\n");
            uint32_t sawTime = storeForwardPlugin->sawNode(mp.from);
            DEBUG_MSG("Last Saw this node %d, %d millis ago\n", mp.from, (millis() - sawTime));
        }

    } else {
        DEBUG_MSG("Store & Forward Plugin - Disabled\n");
    }

#endif

    return true; // Let others look at this message also if they want
}
