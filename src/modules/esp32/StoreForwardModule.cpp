#include "StoreForwardModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "airtime.h"
#include "configuration.h"
#include "memGet.h"
#include "mesh-pb-constants.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"
#include "modules/ModuleDev.h"
#include <Arduino.h>
#include <iterator>
#include <map>

StoreForwardModule *storeForwardModule;

int32_t StoreForwardModule::runOnce()
{
#ifdef ARCH_ESP32
    if (moduleConfig.store_forward.enabled && is_server) {
        // Send out the message queue.
        if (this->busy) {
            // Only send packets if the channel is less than 25% utilized.
            if (airTime->isTxAllowedChannelUtil(true)) {
                storeForwardModule->sendPayload(this->busyTo, this->packetHistoryTXQueue_index);
                if (this->packetHistoryTXQueue_index == packetHistoryTXQueue_size) {
                    // Tell the client we're done sending
                    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
                    sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_PING;
                    storeForwardModule->sendMessage(this->busyTo, sf);
                    LOG_INFO("*** S&F - Done. (ROUTER_PING)\n");
                    this->packetHistoryTXQueue_index = 0;
                    this->busy = false;
                } else {
                    this->packetHistoryTXQueue_index++;
                }
            }
            LOG_DEBUG("*** SF bitrate = %f bytes / sec\n", myNodeInfo.bitrate);

        } else if ((millis() - lastHeartbeat > (heartbeatInterval * 1000)) && airTime->isTxAllowedChannelUtil(true)) {
            lastHeartbeat = millis();
            LOG_INFO("*** Sending heartbeat\n");
            meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
            sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_HEARTBEAT;
            sf.which_variant = meshtastic_StoreAndForward_heartbeat_tag;
            sf.variant.heartbeat.period = 300;
            sf.variant.heartbeat.secondary = 0; // TODO we always have one primary router for now
            storeForwardModule->sendMessage(NODENUM_BROADCAST, sf);
        }
        return (this->packetTimeMax);
    }
#endif
    return disable();
}

/*
    Create our data structure in the PSRAM.
*/
void StoreForwardModule::populatePSRAM()
{
    /*
    For PSRAM usage, see:
        https://learn.upesy.com/en/programmation/psram.html#psram-tab
    */

    LOG_DEBUG("*** Before PSRAM initilization: heap %d/%d PSRAM %d/%d\n", memGet.getFreeHeap(), memGet.getHeapSize(),
              memGet.getFreePsram(), memGet.getPsramSize());

    this->packetHistoryTXQueue =
        static_cast<PacketHistoryStruct *>(ps_calloc(this->historyReturnMax, sizeof(PacketHistoryStruct)));

    /* Use a maximum of 2/3 the available PSRAM unless otherwise specified.
        Note: This needs to be done after every thing that would use PSRAM
    */
    uint32_t numberOfPackets =
        (this->records ? this->records : (((memGet.getFreePsram() / 3) * 2) / sizeof(PacketHistoryStruct)));
    this->records = numberOfPackets;

    this->packetHistory = static_cast<PacketHistoryStruct *>(ps_calloc(numberOfPackets, sizeof(PacketHistoryStruct)));

    LOG_DEBUG("*** After PSRAM initilization: heap %d/%d PSRAM %d/%d\n", memGet.getFreeHeap(), memGet.getHeapSize(),
              memGet.getFreePsram(), memGet.getPsramSize());
    LOG_DEBUG("*** numberOfPackets for packetHistory - %u\n", numberOfPackets);
}

void StoreForwardModule::historySend(uint32_t msAgo, uint32_t to)
{
    uint32_t queueSize = storeForwardModule->historyQueueCreate(msAgo, to);

    if (queueSize) {
        LOG_INFO("*** S&F - Sending %u message(s)\n", queueSize);
        this->busy = true; // runOnce() will pickup the next steps once busy = true.
        this->busyTo = to;
    } else {
        LOG_INFO("*** S&F - No history to send\n");
    }
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_HISTORY;
    sf.which_variant = meshtastic_StoreAndForward_history_tag;
    sf.variant.history.history_messages = queueSize;
    sf.variant.history.window = msAgo;
    storeForwardModule->sendMessage(to, sf);
}

uint32_t StoreForwardModule::historyQueueCreate(uint32_t msAgo, uint32_t to)
{

    this->packetHistoryTXQueue_size = 0;

    for (int i = 0; i < this->packetHistoryCurrent; i++) {
        /*
            LOG_DEBUG("SF historyQueueCreate\n");
            LOG_DEBUG("SF historyQueueCreate - time %d\n", this->packetHistory[i].time);
            LOG_DEBUG("SF historyQueueCreate - millis %d\n", millis());
            LOG_DEBUG("SF historyQueueCreate - math %d\n", (millis() - msAgo));
        */
        if (this->packetHistory[i].time && (this->packetHistory[i].time < (millis() - msAgo))) {
            LOG_DEBUG("*** SF historyQueueCreate - Time matches - ok\n");
            /*
                Copy the messages that were received by the router in the last msAgo
                to the packetHistoryTXQueue structure.

                TODO: The condition (this->packetHistory[i].to & NODENUM_BROADCAST) == to) is not tested since
                I don't have an easy way to target a specific user. Will need to do this soon.
            */
            if ((this->packetHistory[i].to & NODENUM_BROADCAST) == NODENUM_BROADCAST ||
                ((this->packetHistory[i].to & NODENUM_BROADCAST) == to)) {
                this->packetHistoryTXQueue[this->packetHistoryTXQueue_size].time = this->packetHistory[i].time;
                this->packetHistoryTXQueue[this->packetHistoryTXQueue_size].to = this->packetHistory[i].to;
                this->packetHistoryTXQueue[this->packetHistoryTXQueue_size].from = this->packetHistory[i].from;
                this->packetHistoryTXQueue[this->packetHistoryTXQueue_size].channel = this->packetHistory[i].channel;
                this->packetHistoryTXQueue[this->packetHistoryTXQueue_size].payload_size = this->packetHistory[i].payload_size;
                memcpy(this->packetHistoryTXQueue[this->packetHistoryTXQueue_size].payload, this->packetHistory[i].payload,
                       meshtastic_Constants_DATA_PAYLOAD_LEN);
                this->packetHistoryTXQueue_size++;

                LOG_DEBUG("*** PacketHistoryStruct time=%d\n", this->packetHistory[i].time);
                LOG_DEBUG("*** PacketHistoryStruct msg=%s\n", this->packetHistory[i].payload);
            }
        }
    }
    return this->packetHistoryTXQueue_size;
}

void StoreForwardModule::historyAdd(const meshtastic_MeshPacket &mp)
{
    const auto &p = mp.decoded;

    this->packetHistory[this->packetHistoryCurrent].time = millis();
    this->packetHistory[this->packetHistoryCurrent].to = mp.to;
    this->packetHistory[this->packetHistoryCurrent].channel = mp.channel;
    this->packetHistory[this->packetHistoryCurrent].from = mp.from;
    this->packetHistory[this->packetHistoryCurrent].payload_size = p.payload.size;
    memcpy(this->packetHistory[this->packetHistoryCurrent].payload, p.payload.bytes, meshtastic_Constants_DATA_PAYLOAD_LEN);

    this->packetHistoryCurrent++;
    this->packetHistoryMax++;
}

meshtastic_MeshPacket *StoreForwardModule::allocReply()
{
    auto reply = allocDataPacket(); // Allocate a packet for sending
    return reply;
}

void StoreForwardModule::sendPayload(NodeNum dest, uint32_t packetHistory_index)
{
    LOG_INFO("*** Sending S&F Payload\n");
    meshtastic_MeshPacket *p = allocReply();

    p->to = dest;
    p->from = this->packetHistoryTXQueue[packetHistory_index].from;
    p->channel = this->packetHistoryTXQueue[packetHistory_index].channel;

    // Let's assume that if the router received the S&F request that the client is in range.
    //   TODO: Make this configurable.
    p->want_ack = false;

    p->decoded.payload.size =
        this->packetHistoryTXQueue[packetHistory_index].payload_size; // You must specify how many bytes are in the reply
    memcpy(p->decoded.payload.bytes, this->packetHistoryTXQueue[packetHistory_index].payload,
           this->packetHistoryTXQueue[packetHistory_index].payload_size);

    service.sendToMesh(p);
}

void StoreForwardModule::sendMessage(NodeNum dest, meshtastic_StoreAndForward &payload)
{
    meshtastic_MeshPacket *p = allocDataProtobuf(payload);

    p->to = dest;

    p->priority = meshtastic_MeshPacket_Priority_MIN;

    // FIXME - Determine if the delayed packet is broadcast or delayed. For now, assume
    //  everything is broadcast.
    p->delayed = meshtastic_MeshPacket_Delayed_DELAYED_BROADCAST;

    // Let's assume that if the router received the S&F request that the client is in range.
    //   TODO: Make this configurable.
    p->want_ack = false;
    p->decoded.want_response = false;

    service.sendToMesh(p);
}

void StoreForwardModule::sendMessage(NodeNum dest, meshtastic_StoreAndForward_RequestResponse rr)
{
    // Craft an empty response, save some bytes in flash
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = rr;
    storeForwardModule->sendMessage(dest, sf);
}

void StoreForwardModule::statsSend(uint32_t to)
{
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;

    sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_STATS;
    sf.which_variant = meshtastic_StoreAndForward_stats_tag;
    sf.variant.stats.messages_total = this->packetHistoryMax;
    sf.variant.stats.messages_saved = this->packetHistoryCurrent;
    sf.variant.stats.messages_max = this->records;
    sf.variant.stats.up_time = millis() / 1000;
    sf.variant.stats.requests = this->requests;
    sf.variant.stats.requests_history = this->requests_history;
    sf.variant.stats.heartbeat = this->heartbeat;
    sf.variant.stats.return_max = this->historyReturnMax;
    sf.variant.stats.return_window = this->historyReturnWindow;

    LOG_DEBUG("*** Sending S&F Stats\n");
    storeForwardModule->sendMessage(to, sf);
}

ProcessMessage StoreForwardModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#ifdef ARCH_ESP32
    if (moduleConfig.store_forward.enabled) {

        // The router node should not be sending messages as a client. Unless he is a ROUTER_CLIENT
        if ((getFrom(&mp) != nodeDB.getNodeNum()) || (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT)) {

            if ((mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) && is_server) {
                auto &p = mp.decoded;
                if ((p.payload.bytes[0] == 'S') && (p.payload.bytes[1] == 'F') && (p.payload.bytes[2] == 0x00)) {
                    LOG_DEBUG("*** Legacy Request to send\n");

                    // Send the last 60 minutes of messages.
                    if (this->busy) {
                        storeForwardModule->sendMessage(getFrom(&mp), meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY);
                        LOG_INFO("*** S&F - Busy. Try again shortly.\n");
                        meshtastic_MeshPacket *pr = allocReply();
                        pr->to = getFrom(&mp);
                        pr->priority = meshtastic_MeshPacket_Priority_MIN;
                        pr->want_ack = false;
                        pr->decoded.want_response = false;
                        pr->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
                        memcpy(pr->decoded.payload.bytes, "** S&F - Busy. Try again shortly.", 34);
                        pr->decoded.payload.size = 34;
                        service.sendToMesh(pr);
                    } else {
                        storeForwardModule->historySend(historyReturnWindow * 60000, getFrom(&mp));
                    }
                } else {
                    storeForwardModule->historyAdd(mp);
                    LOG_INFO("*** S&F stored. Message history contains %u records now.\n", this->packetHistoryCurrent);
                }

            } else if (mp.decoded.portnum == meshtastic_PortNum_STORE_FORWARD_APP) {
                auto &p = mp.decoded;
                meshtastic_StoreAndForward scratch;
                meshtastic_StoreAndForward *decoded = NULL;
                if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
                    if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_StoreAndForward_msg, &scratch)) {
                        decoded = &scratch;
                    } else {
                        LOG_ERROR("Error decoding protobuf module!\n");
                        // if we can't decode it, nobody can process it!
                        return ProcessMessage::STOP;
                    }
                    return handleReceivedProtobuf(mp, decoded) ? ProcessMessage::STOP : ProcessMessage::CONTINUE;
                }
            } // all others are irrelevant
        }
    }

#endif

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

bool StoreForwardModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_StoreAndForward *p)
{
    if (!moduleConfig.store_forward.enabled) {
        // If this module is not enabled in any capacity, don't handle the packet, and allow other modules to consume
        return false;
    }

    requests++;

    switch (p->rr) {
    case meshtastic_StoreAndForward_RequestResponse_CLIENT_ERROR:
    case meshtastic_StoreAndForward_RequestResponse_CLIENT_ABORT:
        if (is_server) {
            // stop sending stuff, the client wants to abort or has another error
            if ((this->busy) && (this->busyTo == getFrom(&mp))) {
                LOG_ERROR("*** Client in ERROR or ABORT requested\n");
                this->packetHistoryTXQueue_index = 0;
                this->busy = false;
            }
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_HISTORY:
        if (is_server) {
            requests_history++;
            LOG_INFO("*** Client Request to send HISTORY\n");
            // Send the last 60 minutes of messages.
            if (this->busy) {
                storeForwardModule->sendMessage(getFrom(&mp), meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY);
                LOG_INFO("*** S&F - Busy. Try again shortly.\n");
            } else {
                if ((p->which_variant == meshtastic_StoreAndForward_history_tag) && (p->variant.history.window > 0)) {
                    storeForwardModule->historySend(p->variant.history.window * 60000, getFrom(&mp)); // window is in minutes
                } else {
                    storeForwardModule->historySend(historyReturnWindow * 60000, getFrom(&mp)); // defaults to 4 hours
                }
            }
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_PING:
        if (is_server) {
            LOG_INFO("*** StoreAndForward_RequestResponse_CLIENT_PING\n");
            // respond with a ROUTER PONG
            storeForwardModule->sendMessage(getFrom(&mp), meshtastic_StoreAndForward_RequestResponse_ROUTER_PONG);
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_PONG:
        if (is_server) {
            LOG_INFO("*** StoreAndForward_RequestResponse_CLIENT_PONG\n");
            // The Client is alive, update NodeDB
            nodeDB.updateFrom(mp);
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_STATS:
        if (is_server) {
            LOG_INFO("*** Client Request to send STATS\n");
            if (this->busy) {
                storeForwardModule->sendMessage(getFrom(&mp), meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY);
                LOG_INFO("*** S&F - Busy. Try again shortly.\n");
            } else {
                storeForwardModule->statsSend(getFrom(&mp));
            }
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_ERROR:
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY:
        if (is_client) {
            LOG_DEBUG("*** StoreAndForward_RequestResponse_ROUTER_BUSY\n");
            // retry in messages_saved * packetTimeMax ms
            retry_delay = millis() + packetHistoryCurrent * packetTimeMax *
                                         (meshtastic_StoreAndForward_RequestResponse_ROUTER_ERROR ? 2 : 1);
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_PONG:
    // A router responded, this is equal to receiving a heartbeat
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_HEARTBEAT:
        if (is_client) {
            // register heartbeat and interval
            if (p->which_variant == meshtastic_StoreAndForward_heartbeat_tag) {
                heartbeatInterval = p->variant.heartbeat.period;
            }
            lastHeartbeat = millis();
            LOG_INFO("*** StoreAndForward Heartbeat received\n");
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_PING:
        if (is_client) {
            LOG_DEBUG("*** StoreAndForward_RequestResponse_ROUTER_PING\n");
            // respond with a CLIENT PONG
            storeForwardModule->sendMessage(getFrom(&mp), meshtastic_StoreAndForward_RequestResponse_CLIENT_PONG);
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_STATS:
        if (is_client) {
            LOG_DEBUG("*** Router Response STATS\n");
            // These fields only have informational purpose on a client. Fill them to consume later.
            if (p->which_variant == meshtastic_StoreAndForward_stats_tag) {
                this->packetHistoryMax = p->variant.stats.messages_total;
                this->packetHistoryCurrent = p->variant.stats.messages_saved;
                this->records = p->variant.stats.messages_max;
                this->requests = p->variant.stats.requests;
                this->requests_history = p->variant.stats.requests_history;
                this->heartbeat = p->variant.stats.heartbeat;
                this->historyReturnMax = p->variant.stats.return_max;
                this->historyReturnWindow = p->variant.stats.return_window;
            }
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_HISTORY:
        if (is_client) {
            // These fields only have informational purpose on a client. Fill them to consume later.
            if (p->which_variant == meshtastic_StoreAndForward_history_tag) {
                this->historyReturnWindow = p->variant.history.window / 60000;
                LOG_INFO("*** Router Response HISTORY - Sending %d messages from last %d minutes\n",
                         p->variant.history.history_messages, this->historyReturnWindow);
            }
        }
        break;

    default:
        assert(0); // unexpected state
    }
    return true; // There's no need for others to look at this message.
}

StoreForwardModule::StoreForwardModule()
    : concurrency::OSThread("StoreForwardModule"),
      ProtobufModule("StoreForward", meshtastic_PortNum_STORE_FORWARD_APP, &meshtastic_StoreAndForward_msg)
{

#ifdef ARCH_ESP32

    isPromiscuous = true; // Brown chicken brown cow

    if (StoreForward_Dev) {
        /*
            Uncomment the preferences below if you want to use the module
            without having to configure it from the PythonAPI or WebUI.
        */

        moduleConfig.store_forward.enabled = 1;
    }

    if (moduleConfig.store_forward.enabled) {

        // Router
        if ((config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER) ||
            (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT)) {
            LOG_INFO("*** Initializing Store & Forward Module in Router mode\n");
            if (memGet.getPsramSize() > 0) {
                if (memGet.getFreePsram() >= 1024 * 1024) {

                    // Do the startup here

                    // Maximum number of records to return.
                    if (moduleConfig.store_forward.history_return_max)
                        this->historyReturnMax = moduleConfig.store_forward.history_return_max;

                    // Maximum time window for records to return (in minutes)
                    if (moduleConfig.store_forward.history_return_window)
                        this->historyReturnWindow = moduleConfig.store_forward.history_return_window;

                    // Maximum number of records to store in memory
                    if (moduleConfig.store_forward.records)
                        this->records = moduleConfig.store_forward.records;

                    // send heartbeat advertising?
                    if (moduleConfig.store_forward.heartbeat)
                        this->heartbeat = moduleConfig.store_forward.heartbeat;

                    // Popupate PSRAM with our data structures.
                    this->populatePSRAM();
                    is_server = true;
                } else {
                    LOG_INFO("*** Device has less than 1M of PSRAM free.\n");
                    LOG_INFO("*** Store & Forward Module - disabling server.\n");
                }
            } else {
                LOG_INFO("*** Device doesn't have PSRAM.\n");
                LOG_INFO("*** Store & Forward Module - disabling server.\n");
            }

            // Client
        }
        if ((config.device.role == meshtastic_Config_DeviceConfig_Role_CLIENT) ||
            (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT)) {
            is_client = true;
            LOG_INFO("*** Initializing Store & Forward Module in Client mode\n");
        }
    } else {
        disable();
    }
#endif
}
