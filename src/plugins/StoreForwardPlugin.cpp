#include "StoreForwardPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include <Arduino.h>
#include <map>

#define STOREFORWARD_MAX_PACKETS 6000

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

    radioConfig.preferences.store_forward_plugin_enabled = 1;
    radioConfig.preferences.is_router = 1;

    if (radioConfig.preferences.store_forward_plugin_enabled) {

        if (firstTime) {

            /*
             */

            if (radioConfig.preferences.is_router) {
                DEBUG_MSG("Initializing Store & Forward Plugin - Enabled\n");
                // Router
                if (ESP.getPsramSize()) {
                    if (ESP.getFreePsram() >= 2048 * 1024) {
                        // Do the startup here
                        storeForwardPluginRadio = new StoreForwardPluginRadio();

                        firstTime = 0;

                        /*
                        For PSRAM usage, see:
                            https://learn.upesy.com/en/programmation/psram.html#psram-tab
                        */

                        DEBUG_MSG("Total heap: %d\n", ESP.getHeapSize());
                        DEBUG_MSG("Free heap: %d\n", ESP.getFreeHeap());
                        DEBUG_MSG("Total PSRAM: %d\n", ESP.getPsramSize());
                        DEBUG_MSG("Free PSRAM: %d\n", ESP.getFreePsram());

                        PacketHistoryStruct *packetHistory =
                            (PacketHistoryStruct *)ps_calloc(STOREFORWARD_MAX_PACKETS, sizeof(PacketHistoryStruct));

                        DEBUG_MSG("Total heap: %d\n", ESP.getHeapSize());
                        DEBUG_MSG("Free heap: %d\n", ESP.getFreeHeap());
                        DEBUG_MSG("Total PSRAM: %d\n", ESP.getPsramSize());
                        DEBUG_MSG("Free PSRAM: %d\n", ESP.getFreePsram());

                        DEBUG_MSG("packetHistory Size - %u", sizeof(packetHistory));

                        // packetHistory[0].bytes;
                        return (10 * 1000);

                    } else {
                        DEBUG_MSG("Device has less than 2M of PSRAM free. Aborting startup.\n");
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
            this->sawNodeReport();
            return (10 * 1000);
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

    DEBUG_MSG("looking for node - %u\n", node);
    for (int i = 0; i < 50; i++) {
        // DEBUG_MSG("Iterating through the seen nodes - %u %u %u\n", i, receivedRecord[i][0], receivedRecord[i][1]);
        // First time seeing that node.
        if (receivedRecord[i][0] == 0) {
            // DEBUG_MSG("New node! Woohoo! Win!\n");
            receivedRecord[i][0] = node;
            receivedRecord[i][1] = millis();

            return receivedRecord[i][1];
        }

        // We've seen this node before.
        if (receivedRecord[i][0] == node) {
            // DEBUG_MSG("We've seen this node before\n");
            uint32_t lastSaw = receivedRecord[i][1];
            receivedRecord[i][1] = millis();
            return lastSaw;
        }
    }

    return 0;
}

void StoreForwardPlugin::addHistory(const MeshPacket &mp)
{
    auto &p = mp;

    static uint8_t bytes[MAX_RHPACKETLEN];
    size_t numbytes = pb_encode_to_bytes(bytes, sizeof(bytes), SubPacket_fields, &p.decoded);
    assert(numbytes <= MAX_RHPACKETLEN);

    DEBUG_MSG("MP numbytes %u\n", numbytes);

    // destination, source, bytes
    // memcpy(p->encrypted.bytes, bytes, numbytes);

    // pb_decode_from_bytes

    // Serialization is in Router.cpp line 180
}

// We saw a node.
void StoreForwardPlugin::sawNodeReport()
{

    /*
    TODO: Move receivedRecord into the PSRAM

    TODO: Gracefully handle the case where we run out of records.
            Maybe replace the oldest record that hasn't been seen in a while and assume they won't be back.

    TODO: Implment this as a std::map for quicker lookups (maybe it doesn't matter?).
    */

    DEBUG_MSG("Iterating through the seen nodes ...\n");
    for (int i = 0; i < 50; i++) {
        if (receivedRecord[i][1]) {
            DEBUG_MSG("... record-%u node-%u secAgo-%u\n", i, receivedRecord[i][0], (millis() - receivedRecord[i][1]) / 1000);
        }
    }
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

        if (mp.from != nodeDB.getNodeNum()) {
            // DEBUG_MSG("Store & Forward Plugin -- Print Start ---------- ---------- ---------- ---------- ----------\n\n\n");
            printPacket("----- PACKET FROM RADIO -----", &mp);
            uint32_t sawTime = storeForwardPlugin->sawNode(mp.from);
            DEBUG_MSG("We last saw this node (%u), %u sec ago\n", mp.from, (millis() - sawTime) / 1000);

            if (mp.decoded.data.portnum == PortNum_UNKNOWN_APP) {
                DEBUG_MSG("Packet came from - PortNum_UNKNOWN_APP\n");
            } else if (mp.decoded.data.portnum == PortNum_TEXT_MESSAGE_APP) {
                DEBUG_MSG("Packet came from - PortNum_TEXT_MESSAGE_APP\n");

                storeForwardPlugin->addHistory(&mp);

            } else if (mp.decoded.data.portnum == PortNum_REMOTE_HARDWARE_APP) {
                DEBUG_MSG("Packet came from - PortNum_REMOTE_HARDWARE_APP\n");
            } else if (mp.decoded.data.portnum == PortNum_POSITION_APP) {
                DEBUG_MSG("Packet came from - PortNum_POSITION_APP\n");
            } else if (mp.decoded.data.portnum == PortNum_NODEINFO_APP) {
                DEBUG_MSG("Packet came from - PortNum_NODEINFO_APP\n");
            } else if (mp.decoded.data.portnum == PortNum_REPLY_APP) {
                DEBUG_MSG("Packet came from - PortNum_REPLY_APP\n");
            } else if (mp.decoded.data.portnum == PortNum_IP_TUNNEL_APP) {
                DEBUG_MSG("Packet came from - PortNum_IP_TUNNEL_APP\n");
            } else if (mp.decoded.data.portnum == PortNum_SERIAL_APP) {
                DEBUG_MSG("Packet came from - PortNum_SERIAL_APP\n");
            } else if (mp.decoded.data.portnum == PortNum_STORE_FORWARD_APP) {
                DEBUG_MSG("Packet came from - PortNum_STORE_FORWARD_APP\n");
            } else if (mp.decoded.data.portnum == PortNum_RANGE_TEST_APP) {
                DEBUG_MSG("Packet came from - PortNum_RANGE_TEST_APP\n");
            } else if (mp.decoded.data.portnum == PortNum_PRIVATE_APP) {
                DEBUG_MSG("Packet came from - PortNum_PRIVATE_APP\n");
            } else if (mp.decoded.data.portnum == PortNum_RANGE_TEST_APP) {
                DEBUG_MSG("Packet came from - PortNum_RANGE_TEST_APP\n");
            } else if (mp.decoded.data.portnum == PortNum_ATAK_FORWARDER) {
                DEBUG_MSG("Packet came from - PortNum_ATAK_FORWARDER\n");
            } else {
                DEBUG_MSG("Packet came from an unknown port %u\n", mp.decoded.data.portnum);
            }
        }

    } else {
        DEBUG_MSG("Store & Forward Plugin - Disabled\n");
    }

#endif

    return true; // Let others look at this message also if they want
}
