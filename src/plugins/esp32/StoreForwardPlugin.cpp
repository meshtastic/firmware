#include "StoreForwardPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "mesh/generated/storeforward.pb.h"
#include "plugins/PluginDev.h"
#include <Arduino.h>
#include <iterator>
#include <map>

StoreForwardPlugin *storeForwardPlugin;

int32_t StoreForwardPlugin::runOnce()
{

#ifndef NO_ESP32

    if (radioConfig.preferences.store_forward_plugin_enabled) {

        if (radioConfig.preferences.is_router) {

            if (this->busy) {
                // Send out the message queue.

                // DEBUG_MSG("--- --- --- In busy loop 1 %d\n", this->packetHistoryTXQueue_index);
                storeForwardPlugin->sendPayload(this->busyTo, this->packetHistoryTXQueue_index);

                if (this->packetHistoryTXQueue_index == packetHistoryTXQueue_size) {
                    strcpy(this->routerMessage, "** S&F - Done");
                    storeForwardPlugin->sendMessage(this->busyTo, this->routerMessage);
                    // DEBUG_MSG("--- --- --- In busy loop - Done \n");
                    this->packetHistoryTXQueue_index = 0;
                    this->busy = false;
                } else {
                    this->packetHistoryTXQueue_index++;
                }
            }

            // TODO: Dynamicly adjust the time this returns in the loop based on the size of the packets being actually
            // transmitted.
            return (this->packetTimeMax);
        } else {
            DEBUG_MSG("Store & Forward Plugin - Disabled (is_router = false)\n");

            return (INT32_MAX);
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

    uint32_t store_forward_plugin_replay_max_records = 250;

    DEBUG_MSG("Before PSRAM initilization:\n");

    DEBUG_MSG("  Total heap: %d\n", ESP.getHeapSize());
    DEBUG_MSG("  Free heap: %d\n", ESP.getFreeHeap());
    DEBUG_MSG("  Total PSRAM: %d\n", ESP.getPsramSize());
    DEBUG_MSG("  Free PSRAM: %d\n", ESP.getFreePsram());

    // Use a maximum of 2/3 the available PSRAM unless otherwise specified.
    uint32_t numberOfPackets =
        (radioConfig.preferences.store_forward_plugin_records ? radioConfig.preferences.store_forward_plugin_records
                                                              : (((ESP.getFreePsram() / 3) * 2) / sizeof(PacketHistoryStruct)));

    this->packetHistory = static_cast<PacketHistoryStruct *>(ps_calloc(numberOfPackets, sizeof(PacketHistoryStruct)));
    this->packetHistoryTXQueue =
        static_cast<PacketHistoryStruct *>(ps_calloc(store_forward_plugin_replay_max_records, sizeof(PacketHistoryStruct)));
    DEBUG_MSG("After PSRAM initilization:\n");

    DEBUG_MSG("  Total heap: %d\n", ESP.getHeapSize());
    DEBUG_MSG("  Free heap: %d\n", ESP.getFreeHeap());
    DEBUG_MSG("  Total PSRAM: %d\n", ESP.getPsramSize());
    DEBUG_MSG("  Free PSRAM: %d\n", ESP.getFreePsram());
    DEBUG_MSG("Store and Forward Stats:\n");
    DEBUG_MSG("  numberOfPackets for packetHistory - %u\n", numberOfPackets);
}

void StoreForwardPlugin::historyReport()
{
    DEBUG_MSG("Iterating through the message history...\n");
    DEBUG_MSG("Message history contains %u records\n", this->packetHistoryCurrent);
}

/*
 *
 */
void StoreForwardPlugin::historySend(uint32_t msAgo, uint32_t to)
{

    uint32_t packetsSent = 0;

    uint32_t queueSize = storeForwardPlugin->historyQueueCreate(msAgo, to);

    if (queueSize) {
        snprintf(this->routerMessage, 80, "** S&F - Sending %d message(s)", queueSize);
        storeForwardPlugin->sendMessage(to, this->routerMessage);

        this->busy = true; // runOnce() will pickup the next steps once busy = true.
        this->busyTo = to;

    } else {
        strcpy(this->routerMessage, "** S&F - No history to send");
        storeForwardPlugin->sendMessage(to, this->routerMessage);
    }
}

uint32_t StoreForwardPlugin::historyQueueCreate(uint32_t msAgo, uint32_t to)
{

    // uint32_t packetHistoryTXQueueIndex = 0;

    this->packetHistoryTXQueue_size = 0;

    for (int i = 0; i < this->packetHistoryCurrent; i++) {
        /*
            DEBUG_MSG("SF historyQueueCreate\n");
            DEBUG_MSG("SF historyQueueCreate - time %d\n", this->packetHistory[i].time);
            DEBUG_MSG("SF historyQueueCreate - millis %d\n", millis());
            DEBUG_MSG("SF historyQueueCreate - math %d\n", (millis() - msAgo));
        */
        if (this->packetHistory[i].time && (this->packetHistory[i].time < (millis() - msAgo))) {
            DEBUG_MSG("SF historyQueueCreate - Time matches - ok\n");
            /*
                Copy the messages that were received by the router in the last msAgo
                to the packetHistoryTXQueue structure.

                TODO: The condition (this->packetHistory[i].to & 0xffffffff) == to) is not tested since
                I don't have an easy way to target a specific user. Will need to do this soon.
            */
            if ((this->packetHistory[i].to & 0xffffffff) == 0xffffffff || ((this->packetHistory[i].to & 0xffffffff) == to)) {
                this->packetHistoryTXQueue[this->packetHistoryTXQueue_size].time = this->packetHistory[i].time;
                this->packetHistoryTXQueue[this->packetHistoryTXQueue_size].to = this->packetHistory[i].to;
                this->packetHistoryTXQueue[this->packetHistoryTXQueue_size].from = this->packetHistory[i].from;
                this->packetHistoryTXQueue[this->packetHistoryTXQueue_size].payload_size = this->packetHistory[i].payload_size;
                memcpy(this->packetHistoryTXQueue[this->packetHistoryTXQueue_size].payload, this->packetHistory[i].payload,
                       Constants_DATA_PAYLOAD_LEN);
                this->packetHistoryTXQueue_size++;

                DEBUG_MSG("PacketHistoryStruct time=%d\n", this->packetHistory[i].time);
                DEBUG_MSG("PacketHistoryStruct msg=%.*s\n", this->packetHistory[i].payload);
                // DEBUG_MSG("PacketHistoryStruct msg=%.*s\n", this->packetHistoryTXQueue[packetHistoryTXQueueIndex].payload);
            }
        }
    }
    return this->packetHistoryTXQueue_size;
}

void StoreForwardPlugin::historyAdd(const MeshPacket &mp)
{
    auto &p = mp.decoded;

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
    p->from = this->packetHistoryTXQueue[packetHistory_index].from;

    // Let's assume that if the router received the S&F request that the client is in range.
    //   TODO: Make this configurable.
    p->want_ack = false;

    p->decoded.payload.size =
        this->packetHistoryTXQueue[packetHistory_index].payload_size; // You must specify how many bytes are in the reply
    memcpy(p->decoded.payload.bytes, this->packetHistoryTXQueue[packetHistory_index].payload,
           this->packetHistoryTXQueue[packetHistory_index].payload_size);

    service.sendToMesh(p);
}

void StoreForwardPlugin::sendMessage(NodeNum dest, char *str)
{
    MeshPacket *p = allocReply();

    p->to = dest;

    // Let's assume that if the router received the S&F request that the client is in range.
    //   TODO: Make this configurable.
    p->want_ack = false;

    p->decoded.payload.size = strlen(str); // You must specify how many bytes are in the reply
    memcpy(p->decoded.payload.bytes, str, strlen(str));

    service.sendToMesh(p);

    // HardwareMessage_init_default
}

ProcessMessage StoreForwardPlugin::handleReceived(const MeshPacket &mp)
{
#ifndef NO_ESP32
    if (radioConfig.preferences.store_forward_plugin_enabled) {

        DEBUG_MSG("--- S&F Received something\n");

        StoreAndForwardMessage sfm = StoreAndForwardMessage_init_default;

        switch (sfm.rr) {
        }

        auto &p = mp.decoded;

        // The router node should not be sending messages as a client.
        if (getFrom(&mp) != nodeDB.getNodeNum()) {

            if (mp.decoded.portnum == PortNum_TEXT_MESSAGE_APP) {
                DEBUG_MSG("Packet came from - PortNum_TEXT_MESSAGE_APP\n");

                if ((p.payload.bytes[0] == 'S') && (p.payload.bytes[1] == 'F') && (p.payload.bytes[2] == 0x00)) {
                    DEBUG_MSG("--- --- --- Request to send\n");

                    // Send the last 60 minutes of messages.
                    if (this->busy) {
                        strcpy(this->routerMessage, "** S&F - Busy. Try again shortly.");
                        storeForwardPlugin->sendMessage(getFrom(&mp), this->routerMessage);
                    } else {
                        storeForwardPlugin->historySend(1000 * 60, getFrom(&mp));
                    }
                } else if ((p.payload.bytes[0] == 'S') && (p.payload.bytes[1] == 'F') && (p.payload.bytes[2] == 'm') &&
                           (p.payload.bytes[3] == 0x00)) {
                    strcpy(this->routerMessage, "01234567890123456789012345678901234567890123456789012345678901234567890123456789"
                                                "01234567890123456789012345678901234567890123456789012345678901234567890123456789"
                                                "01234567890123456789012345678901234567890123456789012345678901234567890123456");
                    storeForwardPlugin->sendMessage(getFrom(&mp), this->routerMessage);

                } else {
                    storeForwardPlugin->historyAdd(mp);
                }

            } else if (mp.decoded.portnum == PortNum_STORE_FORWARD_APP) {

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

#ifndef NO_ESP32

    isPromiscuous = true; // Brown chicken brown cow

    if (StoreForward_Dev) {
        /*
            Uncomment the preferences below if you want to use the plugin
            without having to configure it from the PythonAPI or WebUI.
        */

        // radioConfig.preferences.store_forward_plugin_enabled = 1;
        // radioConfig.preferences.is_router = 1;
        // radioConfig.preferences.is_always_powered = 1;
    }

    if (radioConfig.preferences.store_forward_plugin_enabled) {

        // Router
        if (radioConfig.preferences.is_router) {
            DEBUG_MSG("Initializing Store & Forward Plugin - Enabled as Router\n");
            if (ESP.getPsramSize()) {
                if (ESP.getFreePsram() >= 1024 * 1024) {

                    // Do the startup here

                    // Popupate PSRAM with our data structures.
                    this->populatePSRAM();

                    // Calculate the packet time.
                    // this->packetTimeMax = RadioLibInterface::instance->getPacketTime(Constants_DATA_PAYLOAD_LEN);
                    // RadioLibInterface::instance->getPacketTime(Constants_DATA_PAYLOAD_LEN);
                    // RadioLibInterface::instance->getPacketTime(Constants_DATA_PAYLOAD_LEN);
                    // RadioInterface::getPacketTime(500)l

                    this->packetTimeMax = 2000;

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
