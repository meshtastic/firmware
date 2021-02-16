#include "RangeTestPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <Arduino.h>
#include <assert.h>

/*
    As a sender, I can send packets every n-seonds. These packets include an incramented PacketID.

    As a receiver, I can receive packets from multiple senders. These packets can be saved to the spiffs.

*/

RangeTestPlugin *rangeTestPlugin;
RangeTestPluginRadio *rangeTestPluginRadio;

RangeTestPlugin::RangeTestPlugin() : concurrency::OSThread("RangeTestPlugin") {}

uint16_t packetSequence = 0;

// char serialStringChar[Constants_DATA_PAYLOAD_LEN];

int32_t RangeTestPlugin::runOnce()
{
#ifndef NO_ESP32

    /*
        Uncomment the preferences below if you want to use the plugin
        without having to configure it from the PythonAPI or WebUI.
    */

    // radioConfig.preferences.range_test_plugin_enabled = 1;
    // radioConfig.preferences.range_test_plugin_sender = 0;
    // radioConfig.preferences.fixed_position = 1;

    uint32_t senderHeartbeat = radioConfig.preferences.range_test_plugin_sender * 1000;

    if (radioConfig.preferences.range_test_plugin_enabled) {

        if (firstTime) {

            // Interface with the serial peripheral from in here.

            rangeTestPluginRadio = new RangeTestPluginRadio();

            firstTime = 0;

            if (radioConfig.preferences.range_test_plugin_sender) {
                DEBUG_MSG("Initializing Range Test Plugin -- Sender\n");
                return (senderHeartbeat);
            } else {
                DEBUG_MSG("Initializing Range Test Plugin -- Receiver\n");
                return (500);
            }

        } else {

            if (radioConfig.preferences.range_test_plugin_sender) {
                // If sender
                DEBUG_MSG("Range Test Plugin - Sending heartbeat every %d ms\n", (senderHeartbeat));

                DEBUG_MSG("gpsStatus->getLatitude()     %d\n", gpsStatus->getLatitude());
                DEBUG_MSG("gpsStatus->getLongitude()    %d\n", gpsStatus->getLongitude());
                DEBUG_MSG("gpsStatus->getHasLock()      %d\n", gpsStatus->getHasLock());
                DEBUG_MSG("gpsStatus->getDOP()          %d\n", gpsStatus->getDOP());
                DEBUG_MSG("gpsStatus->getHasLock()      %d\n", gpsStatus->getHasLock());
                DEBUG_MSG("pref.fixed_position()        %d\n", radioConfig.preferences.fixed_position);

                rangeTestPluginRadio->sendPayload();
                return ((senderHeartbeat));
            } else {
                // Otherwise, we're a receiver.

                return (500);
            }
            // TBD
        }

    } else {
        DEBUG_MSG("Range Test Plugin - Disabled\n");
    }

    return (INT32_MAX);
#endif
}

MeshPacket *RangeTestPluginRadio::allocReply()
{

    auto reply = allocDataPacket(); // Allocate a packet for sending

    return reply;
}

void RangeTestPluginRadio::sendPayload(NodeNum dest, bool wantReplies)
{
    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;

    p->want_ack = true;

    packetSequence++;

    static char heartbeatString[20];
    snprintf(heartbeatString, sizeof(heartbeatString), "seq %d", packetSequence);

    p->decoded.data.payload.size = strlen(heartbeatString); // You must specify how many bytes are in the reply
    memcpy(p->decoded.data.payload.bytes, heartbeatString, p->decoded.data.payload.size);

    service.sendToMesh(p);
}

bool RangeTestPluginRadio::handleReceived(const MeshPacket &mp)
{
#ifndef NO_ESP32

    if (radioConfig.preferences.range_test_plugin_enabled) {

        auto &p = mp.decoded.data;
        // DEBUG_MSG("Received text msg self=0x%0x, from=0x%0x, to=0x%0x, id=%d, msg=%.*s\n",
        //          nodeDB.getNodeNum(), mp.from, mp.to, mp.id, p.payload.size, p.payload.bytes);

        if (mp.from != nodeDB.getNodeNum()) {

            // DEBUG_MSG("* * Message came from the mesh\n");
            // Serial2.println("* * Message came from the mesh");
            // Serial2.printf("%s", p.payload.bytes);
            /*



            */

            NodeInfo *n = nodeDB.getNode(mp.from);


            DEBUG_MSG("-----------------------------------------\n");
            DEBUG_MSG("p.payload.bytes  \"%s\"\n", p.payload.bytes);
            DEBUG_MSG("p.payload.size   %d\n", p.payload.size);
            DEBUG_MSG("---- Received Packet:\n");
            DEBUG_MSG("mp.from          %d\n", mp.from);
            DEBUG_MSG("mp.rx_snr        %f\n", mp.rx_snr);
            DEBUG_MSG("mp.hop_limit     %d\n", mp.hop_limit);
            //deprecated and unpopulated for sometime
            //DEBUG_MSG("mp.decoded.position.latitude_i     %d\n", mp.decoded.position.latitude_i);
            //DEBUG_MSG("mp.decoded.position.longitude_i    %d\n", mp.decoded.position.longitude_i);
            DEBUG_MSG("---- Node Information of Received Packet (mp.from):\n");
            DEBUG_MSG("n->user.long_name         %s\n", n->user.long_name);
            DEBUG_MSG("n->user.short_name        %s\n", n->user.short_name);
            DEBUG_MSG("n->user.macaddr           %X\n", n->user.macaddr);
            DEBUG_MSG("n->has_position           %d\n", n->has_position);
            DEBUG_MSG("n->position.latitude_i    %d\n", n->position.latitude_i);
            DEBUG_MSG("n->position.longitude_i   %d\n", n->position.longitude_i);
            DEBUG_MSG("n->position.battery_level %d\n", n->position.battery_level);
            DEBUG_MSG("---- Current device location information:\n");
            DEBUG_MSG("gpsStatus->getLatitude()     %d\n", gpsStatus->getLatitude());
            DEBUG_MSG("gpsStatus->getLongitude()    %d\n", gpsStatus->getLongitude());
            DEBUG_MSG("gpsStatus->getHasLock()      %d\n", gpsStatus->getHasLock());
            DEBUG_MSG("gpsStatus->getDOP()          %d\n", gpsStatus->getDOP());
            DEBUG_MSG("-----------------------------------------\n");
        }

    } else {
        DEBUG_MSG("Range Test Plugin Disabled\n");
    }

#endif

    return true; // Let others look at this message also if they want
}
