#include "StoreForwardModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "airtime.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "mesh/generated/storeforward.pb.h"
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
            if (airTime->channelUtilizationPercent() < polite_channel_util_percent) {
                storeForwardModule->sendPayload(this->busyTo, this->packetHistoryTXQueue_index);
                if (this->packetHistoryTXQueue_index == packetHistoryTXQueue_size) {
                    // Tell the client we're done sending
                    StoreAndForward sf = StoreAndForward_init_zero;
                    sf.rr = StoreAndForward_RequestResponse_ROUTER_PING;
                    storeForwardModule->sendMessage(this->busyTo, sf);
                    DEBUG_MSG("*** S&F - Done. (ROUTER_PING)\n");
                    this->packetHistoryTXQueue_index = 0;
                    this->busy = false;
                } else {
                    this->packetHistoryTXQueue_index++;
                }
            } else {
                DEBUG_MSG("*** Channel utilization is too high. Retrying later.\n");
            }
            DEBUG_MSG("*** SF bitrate = %f bytes / sec\n", myNodeInfo.bitrate);

        } else if ((millis() - lastHeartbeat > (heartbeatInterval * 1000)) && (airTime->channelUtilizationPercent() < polite_channel_util_percent)) {
            lastHeartbeat = millis();
            DEBUG_MSG("*** Sending heartbeat\n");
            StoreAndForward sf = StoreAndForward_init_zero;
            sf.rr = StoreAndForward_RequestResponse_ROUTER_HEARTBEAT;
            sf.which_variant = StoreAndForward_heartbeat_tag;
            sf.variant.heartbeat.period = 300;
            sf.variant.heartbeat.secondary = 0; // TODO we always have one primary router for now
            storeForwardModule->sendMessage(NODENUM_BROADCAST, sf);
        }
        return (this->packetTimeMax);
    }
#endif
    return (INT32_MAX);
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

    DEBUG_MSG("*** Before PSRAM initilization: heap %d/%d PSRAM %d/%d\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getFreePsram(), ESP.getPsramSize());

    this->packetHistoryTXQueue =
        static_cast<PacketHistoryStruct *>(ps_calloc(this->historyReturnMax, sizeof(PacketHistoryStruct)));

    /* Use a maximum of 2/3 the available PSRAM unless otherwise specified.
        Note: This needs to be done after every thing that would use PSRAM
    */
    uint32_t numberOfPackets = (this->records ? this->records : (((ESP.getFreePsram() / 3) * 2) / sizeof(PacketHistoryStruct)));
    this->records = numberOfPackets;

    this->packetHistory = static_cast<PacketHistoryStruct *>(ps_calloc(numberOfPackets, sizeof(PacketHistoryStruct)));

    DEBUG_MSG("*** After PSRAM initilization: heap %d/%d PSRAM %d/%d\n", ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getFreePsram(), ESP.getPsramSize());
    DEBUG_MSG("*** numberOfPackets for packetHistory - %u\n", numberOfPackets);
}

void StoreForwardModule::historySend(uint32_t msAgo, uint32_t to)
{
    uint32_t queueSize = storeForwardModule->historyQueueCreate(msAgo, to);

    if (queueSize) {
        DEBUG_MSG ("*** S&F - Sending %u message(s)\n", queueSize);
        this->busy = true; // runOnce() will pickup the next steps once busy = true.
        this->busyTo = to;
    } else {
        DEBUG_MSG ("*** S&F - No history to send\n");
    }
    StoreAndForward sf = StoreAndForward_init_zero;
    sf.rr = StoreAndForward_RequestResponse_ROUTER_HISTORY;
    sf.which_variant = StoreAndForward_history_tag;
    sf.variant.history.history_messages = queueSize;
    sf.variant.history.window = msAgo;
    storeForwardModule->sendMessage(to, sf);
}

uint32_t StoreForwardModule::historyQueueCreate(uint32_t msAgo, uint32_t to)
{

    this->packetHistoryTXQueue_size = 0;

    for (int i = 0; i < this->packetHistoryCurrent; i++) {
        /*
            DEBUG_MSG("SF historyQueueCreate\n");
            DEBUG_MSG("SF historyQueueCreate - time %d\n", this->packetHistory[i].time);
            DEBUG_MSG("SF historyQueueCreate - millis %d\n", millis());
            DEBUG_MSG("SF historyQueueCreate - math %d\n", (millis() - msAgo));
        */
        if (this->packetHistory[i].time && (this->packetHistory[i].time < (millis() - msAgo))) {
            DEBUG_MSG("*** SF historyQueueCreate - Time matches - ok\n");
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
                       Constants_DATA_PAYLOAD_LEN);
                this->packetHistoryTXQueue_size++;

                DEBUG_MSG("*** PacketHistoryStruct time=%d\n", this->packetHistory[i].time);
                DEBUG_MSG("*** PacketHistoryStruct msg=%s\n", this->packetHistory[i].payload);
            }
        }
    }
    return this->packetHistoryTXQueue_size;
}

void StoreForwardModule::historyAdd(const MeshPacket &mp)
{
    const auto &p = mp.decoded;

    this->packetHistory[this->packetHistoryCurrent].time = millis();
    this->packetHistory[this->packetHistoryCurrent].to = mp.to;
    this->packetHistory[this->packetHistoryCurrent].channel = mp.channel;
    this->packetHistory[this->packetHistoryCurrent].from = mp.from;
    this->packetHistory[this->packetHistoryCurrent].payload_size = p.payload.size;
    memcpy(this->packetHistory[this->packetHistoryCurrent].payload, p.payload.bytes, Constants_DATA_PAYLOAD_LEN);

    this->packetHistoryCurrent++;
    this->packetHistoryMax++;
}

MeshPacket *StoreForwardModule::allocReply()
{
    auto reply = allocDataPacket(); // Allocate a packet for sending
    return reply;
}

void StoreForwardModule::sendPayload(NodeNum dest, uint32_t packetHistory_index)
{
    DEBUG_MSG("*** Sending S&F Payload\n");
    MeshPacket *p = allocReply();

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

void StoreForwardModule::sendMessage(NodeNum dest, StoreAndForward &payload)
{
    MeshPacket *p = allocDataProtobuf(payload);

    p->to = dest;

    p->priority = MeshPacket_Priority_MIN;

    // FIXME - Determine if the delayed packet is broadcast or delayed. For now, assume
    //  everything is broadcast.
    p->delayed = MeshPacket_Delayed_DELAYED_BROADCAST;

    // Let's assume that if the router received the S&F request that the client is in range.
    //   TODO: Make this configurable.
    p->want_ack = false;
    p->decoded.want_response = false;

    service.sendToMesh(p);
}

void StoreForwardModule::sendMessage(NodeNum dest, StoreAndForward_RequestResponse rr)
{
    // Craft an empty response, save some bytes in flash
    StoreAndForward sf = StoreAndForward_init_zero;
    sf.rr = rr;
    storeForwardModule->sendMessage(dest, sf);
}

void StoreForwardModule::statsSend(uint32_t to)
{
    StoreAndForward sf = StoreAndForward_init_zero;

    sf.rr = StoreAndForward_RequestResponse_ROUTER_STATS;
    sf.which_variant = StoreAndForward_stats_tag;
    sf.variant.stats.messages_total = this->packetHistoryMax;
    sf.variant.stats.messages_saved = this->packetHistoryCurrent;
    sf.variant.stats.messages_max = this->records;
    sf.variant.stats.up_time = millis() / 1000;
    sf.variant.stats.requests = this->requests;
    sf.variant.stats.requests_history = this->requests_history;
    sf.variant.stats.heartbeat = this->heartbeat;
    sf.variant.stats.return_max = this->historyReturnMax;
    sf.variant.stats.return_window = this->historyReturnWindow;

    DEBUG_MSG("*** Sending S&F Stats\n");
    storeForwardModule->sendMessage(to, sf);
}

ProcessMessage StoreForwardModule::handleReceived(const MeshPacket &mp)
{
#ifdef ARCH_ESP32
    if (moduleConfig.store_forward.enabled) {

        // The router node should not be sending messages as a client. Unless he is a ROUTER_CLIENT
        if ((getFrom(&mp) != nodeDB.getNodeNum()) || (config.device.role == Config_DeviceConfig_Role_ROUTER_CLIENT)) {

            if (mp.decoded.portnum == PortNum_TEXT_MESSAGE_APP) {
                storeForwardModule->historyAdd(mp);
                DEBUG_MSG("*** S&F stored. Message history contains %u records now.\n", this->packetHistoryCurrent);

            } else if (mp.decoded.portnum == PortNum_STORE_FORWARD_APP) {
                auto &p = mp.decoded;
                StoreAndForward scratch;
                StoreAndForward *decoded = NULL;
                if (mp.which_payload_variant == MeshPacket_decoded_tag) {
                    if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &StoreAndForward_msg, &scratch)) {
                        decoded = &scratch;
                    } else {
                        DEBUG_MSG("Error decoding protobuf module!\n");
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

bool StoreForwardModule::handleReceivedProtobuf(const MeshPacket &mp, StoreAndForward *p)
{
    if (!moduleConfig.store_forward.enabled) {
        // If this module is not enabled in any capacity, don't handle the packet, and allow other modules to consume
        return false;
    }

    requests++;

    switch (p->rr) {
        case StoreAndForward_RequestResponse_CLIENT_ERROR:
        case StoreAndForward_RequestResponse_CLIENT_ABORT:
            if(is_server) {
                // stop sending stuff, the client wants to abort or has another error
                if ((this->busy) && (this->busyTo == getFrom(&mp))) {
                    DEBUG_MSG("*** Client in ERROR or ABORT requested\n");
                    this->packetHistoryTXQueue_index = 0;
                    this->busy = false;
                }
            }
            break;

        case StoreAndForward_RequestResponse_CLIENT_HISTORY:
            if(is_server) {
                requests_history++;
                DEBUG_MSG("*** Client Request to send HISTORY\n");
                // Send the last 60 minutes of messages.
                if (this->busy) {
                    storeForwardModule->sendMessage(getFrom(&mp), StoreAndForward_RequestResponse_ROUTER_BUSY);
                    DEBUG_MSG("*** S&F - Busy. Try again shortly.\n");
                } else {
                    if ((p->which_variant == StoreAndForward_history_tag) && (p->variant.history.window > 0)){
                        storeForwardModule->historySend(p->variant.history.window * 60000, getFrom(&mp)); // window is in minutes
                    } else {
                        storeForwardModule->historySend(historyReturnWindow * 60000, getFrom(&mp)); // defaults to 4 hours
                    }
                }
            }
            break;

        case StoreAndForward_RequestResponse_CLIENT_PING:
            if(is_server) {
                DEBUG_MSG("*** StoreAndForward_RequestResponse_CLIENT_PING\n");
                // respond with a ROUTER PONG
                storeForwardModule->sendMessage(getFrom(&mp), StoreAndForward_RequestResponse_ROUTER_PONG);
            }
            break;

        case StoreAndForward_RequestResponse_CLIENT_PONG:
            if(is_server) {
                DEBUG_MSG("*** StoreAndForward_RequestResponse_CLIENT_PONG\n");
                // The Client is alive, update NodeDB
                nodeDB.updateFrom(mp);
            }
            break;

        case StoreAndForward_RequestResponse_CLIENT_STATS:
            if(is_server) {
                DEBUG_MSG("*** Client Request to send STATS\n");
                if (this->busy) {
                    storeForwardModule->sendMessage(getFrom(&mp), StoreAndForward_RequestResponse_ROUTER_BUSY);
                    DEBUG_MSG("*** S&F - Busy. Try again shortly.\n");
                } else {
                    storeForwardModule->statsSend(getFrom(&mp));
                }
            }
            break;

        case StoreAndForward_RequestResponse_ROUTER_ERROR:
        case StoreAndForward_RequestResponse_ROUTER_BUSY:
            if(is_client) {
                DEBUG_MSG("*** StoreAndForward_RequestResponse_ROUTER_BUSY\n");
                // retry in messages_saved * packetTimeMax ms
                retry_delay = millis() + packetHistoryCurrent * packetTimeMax * (StoreAndForward_RequestResponse_ROUTER_ERROR ? 2 : 1);
            }
            break;

        case StoreAndForward_RequestResponse_ROUTER_PONG:
        // A router responded, this is equal to receiving a heartbeat
        case StoreAndForward_RequestResponse_ROUTER_HEARTBEAT:
            if(is_client) {
                // register heartbeat and interval
                if (p->which_variant == StoreAndForward_heartbeat_tag) {
                    heartbeatInterval = p->variant.heartbeat.period;
                }
                lastHeartbeat = millis();
                DEBUG_MSG("*** StoreAndForward Heartbeat received\n");
            }
            break;

        case StoreAndForward_RequestResponse_ROUTER_PING:
            if(is_client) {
                DEBUG_MSG("*** StoreAndForward_RequestResponse_ROUTER_PING\n");
                // respond with a CLIENT PONG
                storeForwardModule->sendMessage(getFrom(&mp), StoreAndForward_RequestResponse_CLIENT_PONG);
            }
            break;

        case StoreAndForward_RequestResponse_ROUTER_STATS:
            if(is_client) {
                DEBUG_MSG("*** Router Response STATS\n");
                // These fields only have informational purpose on a client. Fill them to consume later.
                if (p->which_variant == StoreAndForward_stats_tag) {
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

        case StoreAndForward_RequestResponse_ROUTER_HISTORY:
            if(is_client) {
                // These fields only have informational purpose on a client. Fill them to consume later.
                if (p->which_variant == StoreAndForward_history_tag) {
                    this->historyReturnWindow = p->variant.history.window / 60000;
                    DEBUG_MSG("*** Router Response HISTORY - Sending %d messages from last %d minutes\n", p->variant.history.history_messages, this->historyReturnWindow);
                }
            }
            break;

        default:
            assert(0); // unexpected state - FIXME, make an error code and reboot
    }
    return true; // There's no need for others to look at this message.
}

StoreForwardModule::StoreForwardModule()
    : concurrency::OSThread("StoreForwardModule"), ProtobufModule("StoreForward", PortNum_STORE_FORWARD_APP, &StoreAndForward_msg)
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
        if ((config.device.role == Config_DeviceConfig_Role_ROUTER) || (config.device.role == Config_DeviceConfig_Role_ROUTER_CLIENT)) {
            DEBUG_MSG("*** Initializing Store & Forward Module in Router mode\n");
            if (ESP.getPsramSize() > 0) {
                if (ESP.getFreePsram() >= 1024 * 1024) {

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
                    DEBUG_MSG("*** Device has less than 1M of PSRAM free.\n");
                    DEBUG_MSG("*** Store & Forward Module - disabling server.\n");
                }
            } else {
                DEBUG_MSG("*** Device doesn't have PSRAM.\n");
                DEBUG_MSG("*** Store & Forward Module - disabling server.\n");
            }

            // Client
        }
        if ((config.device.role == Config_DeviceConfig_Role_CLIENT) || (config.device.role == Config_DeviceConfig_Role_ROUTER_CLIENT)) {
            is_client = true;
            DEBUG_MSG("*** Initializing Store & Forward Module in Client mode\n");
        }
    }
#endif
}
