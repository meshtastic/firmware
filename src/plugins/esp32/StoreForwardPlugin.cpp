#include "StoreForwardPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "plugins/PluginDev.h"
#include <Arduino.h>
#include <map>

//#define STOREFORWARD_MAX_PACKETS 0
#define STOREFORWARD_SEND_HISTORY_PERIOD 10 * 60
#define STOREFORWARD_SEND_HISTORY_MAX 0
#define STOREFORWARD_HOP_MAX 0 // How many hops should we allow the packet to be forwarded?

/*
    TODO:
        - Be able to identify if you're within range of a router.
        - Be able to specify the HOP MAX to reduce airtime.
        - Restrict operation of S&F on the slow channel configurations.
        - Build in rate limiting -- Don't allow a user to repeatedly request the history.

    DONE:
        Allow max history to be defined by radioConfig.preferences.store_forward_plugin_records


*/

StoreForwardPlugin *storeForwardPlugin;

int32_t StoreForwardPlugin::runOnce()
{

#ifndef NO_ESP32

    if (radioConfig.preferences.store_forward_plugin_enabled) {

        if (radioConfig.preferences.is_router) {
            // Maybe some cleanup functions?
            this->historyReport();
            return (60 * 1000);
        } else {
            /*
             * If the plugin is turned on and is_router is not enabled, then we'll send a heartbeat every
             * few minutes.
             *
             * This behavior is expected to change. It's only here until we come up with something better.
             */

            //DEBUG_MSG("Store & Forward Plugin - Sending heartbeat\n");

            //storeForwardPlugin->sendPayload();

            return (4 * 60 * 1000);
        }

    } else {
        DEBUG_MSG("Store & Forward Plugin - Disabled\n");

        return (INT32_MAX);
    }

#endif
    return (INT32_MAX);
}

/*
    Create our data structure in the PSRAM.
*/
void StoreForwardPlugin::populatePSRAM()
{
    /*
    For PSRAM usage, see:
        https://learn.upesy.com/en/programmation/psram.html#psram-tab
    */

    DEBUG_MSG("Before PSRAM initilization:\n");

    DEBUG_MSG("  Total heap: %d\n", ESP.getHeapSize());
    DEBUG_MSG("  Free heap: %d\n", ESP.getFreeHeap());
    DEBUG_MSG("  Total PSRAM: %d\n", ESP.getPsramSize());
    DEBUG_MSG("  Free PSRAM: %d\n", ESP.getFreePsram());

    // Use a maximum of 2/3 the available PSRAM unless otherwise specified.
    uint32_t numberOfPackets =( radioConfig.preferences.store_forward_plugin_records ? radioConfig.preferences.store_forward_plugin_records :
        ( ( (ESP.getFreePsram() / 3) * 2 )/ sizeof(PacketHistoryStruct)) );

    // this->packetHistory = (PacketHistoryStruct *)ps_calloc(numberOfPackets, sizeof(PacketHistoryStruct));
    this->packetHistory = static_cast<PacketHistoryStruct *>(ps_calloc(numberOfPackets, sizeof(PacketHistoryStruct)));
    DEBUG_MSG("After PSRAM initilization:\n");

    DEBUG_MSG("  Total heap: %d\n", ESP.getHeapSize());
    DEBUG_MSG("  Free heap: %d\n", ESP.getFreeHeap());
    DEBUG_MSG("  Total PSRAM: %d\n", ESP.getPsramSize());
    DEBUG_MSG("  Free PSRAM: %d\n", ESP.getFreePsram());
    DEBUG_MSG("Store and Forward Stats:\n");
    DEBUG_MSG("  numberOfPackets - %u\n", numberOfPackets);
}

void StoreForwardPlugin::historyReport()
{
    DEBUG_MSG("Iterating through the message history...\n");
    DEBUG_MSG("Message history contains %u records\n", this->packetHistoryCurrent);
    /*
    uint32_t startTimer = millis();
    for (int i = 0; i < this->packetHistoryCurrent; i++) {
        if (this->packetHistory[i].time) {
            // DEBUG_MSG("... time-%u to-0x%08x\n", this->packetHistory[i].time, this->packetHistory[i].to & 0xffffffff);
        }
    }
    DEBUG_MSG("StoreForwardPlugin::historyReport runtime - %u ms\n", millis() - startTimer);
    */
}

/*
 *
 */
void StoreForwardPlugin::historySend(uint32_t msAgo, uint32_t to)
{

    //MeshPacket mp;
    for (int i = 0; i < this->packetHistoryCurrent; i++) {
        if (this->packetHistory[i].time) {

            /*
                Stored packet was sent to a broadcast address
            */
            if ((this->packetHistory[i].to & 0xffffffff) == 0xffffffff) {
                DEBUG_MSG("Request: to-0x%08x, Stored: time-%u to-0x%08x\n", to & 0xffffffff, this->packetHistory[i].time,
                          this->packetHistory[i].to & 0xffffffff);

                DEBUG_MSG(">>>>> %s\n", this->packetHistory[i].payload);

                storeForwardPlugin->sendPayload(to, i);
            }

            /*
                Stored packet was intended to a named address

                TODO: TEST ME! I don't know if this works.
            */
            if ((this->packetHistory[i].to & 0xffffffff) == to) {
                DEBUG_MSG("Request: to-0x%08x, Stored: time-%u to-0x%08x\n", to & 0xffffffff, this->packetHistory[i].time,
                          this->packetHistory[i].to & 0xffffffff);
                storeForwardPlugin->sendPayload(to, i);
            }

            // break;
        }
    }
}

void StoreForwardPlugin::historyAdd(const MeshPacket &mp)
{
    //auto &p = *mp.decoded;
    auto &p = mp.decoded;

    //uint16_t payloadSize = p.payload.size;

    this->packetHistory[this->packetHistoryCurrent].time = millis();
    this->packetHistory[this->packetHistoryCurrent].to = mp.to;
    this->packetHistory[this->packetHistoryCurrent].from = mp.from;
    this->packetHistory[this->packetHistoryCurrent].payload_size = p.payload.size;
    memcpy(this->packetHistory[this->packetHistoryCurrent].payload, p.payload.bytes, Constants_DATA_PAYLOAD_LEN);

    this->packetHistoryCurrent++;
}

MeshPacket *StoreForwardPlugin::allocReply()
{
    auto reply = allocDataPacket(); // Allocate a packet for sending
    return reply;
}

void StoreForwardPlugin::sendPayload(NodeNum dest, uint32_t packetHistory_index)
{
    DEBUG_MSG("Sending S&F Payload\n");
    MeshPacket *p = allocReply();

    p->to = dest;
    p->from = this->packetHistory[packetHistory_index].from;
    
    // Let's assume that if the router received the S&F request that the client is in range.
    //   TODO: Make this configurable.
    p->want_ack = false; 

    //static char heartbeatString[20];
    //snprintf(heartbeatString, sizeof(heartbeatString), "From SF");

    p->decoded.payload.size = this->packetHistory[packetHistory_index].payload_size; // You must specify how many bytes are in the reply
    memcpy(p->decoded.payload.bytes, this->packetHistory[packetHistory_index].payload, this->packetHistory[packetHistory_index].payload_size);

    service.sendToMesh(p);
}

ProcessMessage StoreForwardPlugin::handleReceived(const MeshPacket &mp)
{
#ifndef NO_ESP32
    if (radioConfig.preferences.store_forward_plugin_enabled) {

        DEBUG_MSG("--- S&F Received something\n");

        auto &p = mp.decoded;

        // The router node should not be sending messages as a client.
        if (getFrom(&mp) != nodeDB.getNodeNum()) {
            printPacket("PACKET FROM RADIO", &mp);
            // DEBUG_MSG("We last saw this node (%u), %u sec ago\n", mp.from & 0xffffffff, (millis() - sawTime) / 1000);
            // DEBUG_MSG("    --------------   ");
            if (mp.decoded.portnum == PortNum_TEXT_MESSAGE_APP) {
                DEBUG_MSG("Packet came from - PortNum_TEXT_MESSAGE_APP\n");

                DEBUG_MSG("--- --- --- %s \n", p.payload.bytes);

                if ((p.payload.bytes[0] == 'S') && (p.payload.bytes[1] == 'F')) {
                    DEBUG_MSG("--- --- --- Request to send\n");

                    // Send the last 5 minutes of messages.
                    storeForwardPlugin->historySend(5 * 1000 * 60, getFrom(&mp));
                } else {
                    storeForwardPlugin->historyAdd(mp);
                }

            } else {
                DEBUG_MSG("Packet came from an unknown port %u\n", mp.decoded.portnum);
            }
        }

    } else {
        DEBUG_MSG("Store & Forward Plugin - Disabled\n");
    }

#endif

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

StoreForwardPlugin::StoreForwardPlugin()
    : SinglePortPlugin("StoreForwardPlugin", PortNum_TEXT_MESSAGE_APP), concurrency::OSThread("StoreForwardPlugin")
{
    // StoreForwardPlugin::StoreForwardPlugin()
    //     : SinglePortPlugin("StoreForwardPlugin", PortNum_STORE_FORWARD_APP), concurrency::OSThread("StoreForwardPlugin")
    //{

#ifndef NO_ESP32

    isPromiscuous = true; // Brown chicken brown cow

    if (StoreForward_Dev) {
        /*
            Uncomment the preferences below if you want to use the plugin
            without having to configure it from the PythonAPI or WebUI.
        */

        radioConfig.preferences.store_forward_plugin_enabled = 1;
        radioConfig.preferences.is_router = 1;
        radioConfig.preferences.is_always_powered = 1;
        
    }

    if (radioConfig.preferences.store_forward_plugin_enabled) {

        // Router
        if (radioConfig.preferences.is_router) {
            DEBUG_MSG("Initializing Store & Forward Plugin - Enabled as Router\n");
            if (ESP.getPsramSize()) {
                if (ESP.getFreePsram() >= 1024 * 1024) {

                    // Do the startup here

                    this->populatePSRAM();
                } else {
                    DEBUG_MSG("Device has less than 1M of PSRAM free. Aborting startup.\n");
                    DEBUG_MSG("Store & Forward Plugin - Aborting Startup.\n");
                }

            } else {
                DEBUG_MSG("Device doesn't have PSRAM.\n");
                DEBUG_MSG("Store & Forward Plugin - Aborting Startup.\n");
            }

            // Client
        } else {
            DEBUG_MSG("Initializing Store & Forward Plugin - Enabled as Client\n");
        }
    }
#endif
}