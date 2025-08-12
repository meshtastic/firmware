/**
 * @file StoreForwardModule.cpp
 * @brief Implementation of the StoreForwardModule class.
 *
 * This file contains the implementation of the StoreForwardModule class, which is responsible for managing the store and forward
 * functionality of the Meshtastic device. The class provides methods for sending and receiving messages, as well as managing the
 * message history queue. It also initializes and manages the data structures used for storing the message history.
 *
 * The StoreForwardModule class is used by the MeshService class to provide store and forward functionality to the Meshtastic
 * device.
 *
 * @author Jm Casler
 * @date [Insert Date]
 */
#include "StoreForwardModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "Throttle.h"
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
#if defined(ARCH_ESP32) || defined(ARCH_PORTDUINO)
    if (moduleConfig.store_forward.enabled && is_server) {
        // Send out the message queue.
        if (this->busy) {
            // Only send packets if the channel is less than 25% utilized and until historyReturnMax
            if (airTime->isTxAllowedChannelUtil(true) && this->requestCount < this->historyReturnMax) {
                if (!storeForwardModule->sendPayload(this->busyTo, this->last_time)) {
                    this->requestCount = 0;
                    this->busy = false;
                }
            }
        } else if (this->heartbeat && (!Throttle::isWithinTimespanMs(lastHeartbeat, heartbeatInterval * 1000)) &&
                   airTime->isTxAllowedChannelUtil(true)) {
            lastHeartbeat = millis();
            LOG_INFO("Send heartbeat");
            meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
            sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_HEARTBEAT;
            sf.which_variant = meshtastic_StoreAndForward_heartbeat_tag;
            sf.variant.heartbeat.period = heartbeatInterval;
            sf.variant.heartbeat.secondary = 0; // TODO we always have one primary router for now
            storeForwardModule->sendMessage(NODENUM_BROADCAST, sf);
        }
        return (this->packetTimeMax);
    }
#endif
    return disable();
}

/**
 * Populates the PSRAM with data to be sent later when a device is out of range.
 */
void StoreForwardModule::populatePSRAM()
{
    /*
    For PSRAM usage, see:
        https://learn.upesy.com/en/programmation/psram.html#psram-tab
    */

    LOG_DEBUG("Before PSRAM init: heap %d/%d PSRAM %d/%d", memGet.getFreeHeap(), memGet.getHeapSize(), memGet.getFreePsram(),
              memGet.getPsramSize());

    /* Use a maximum of 3/4 the available PSRAM unless otherwise specified.
        Note: This needs to be done after every thing that would use PSRAM
    */
    uint32_t numberOfPackets =
        (this->records ? this->records : (((memGet.getFreePsram() / 4) * 3) / sizeof(PacketHistoryStruct)));
    this->records = numberOfPackets;
#if defined(ARCH_ESP32)
    this->packetHistory = static_cast<PacketHistoryStruct *>(ps_calloc(numberOfPackets, sizeof(PacketHistoryStruct)));
#elif defined(ARCH_PORTDUINO)
    this->packetHistory = static_cast<PacketHistoryStruct *>(calloc(numberOfPackets, sizeof(PacketHistoryStruct)));

#endif

    LOG_DEBUG("After PSRAM init: heap %d/%d PSRAM %d/%d", memGet.getFreeHeap(), memGet.getHeapSize(), memGet.getFreePsram(),
              memGet.getPsramSize());
    LOG_DEBUG("numberOfPackets for packetHistory - %u", numberOfPackets);
}

/**
 * Sends messages from the message history to the specified recipient.
 *
 * @param sAgo The number of seconds ago from which to start sending messages.
 * @param to The recipient ID to send the messages to.
 */
void StoreForwardModule::historySend(uint32_t secAgo, uint32_t to)
{
    this->last_time = getTime() < secAgo ? 0 : getTime() - secAgo;
    uint32_t queueSize = getNumAvailablePackets(to, last_time);
    if (queueSize > this->historyReturnMax)
        queueSize = this->historyReturnMax;

    if (queueSize) {
        LOG_INFO("S&F - Send %u message(s)", queueSize);
        this->busy = true; // runOnce() will pickup the next steps once busy = true.
        this->busyTo = to;
    } else {
        LOG_INFO("S&F - No history");
    }
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_HISTORY;
    sf.which_variant = meshtastic_StoreAndForward_history_tag;
    sf.variant.history.history_messages = queueSize;
    sf.variant.history.window = secAgo * 1000;
    sf.variant.history.last_request = lastRequest[to];
    storeForwardModule->sendMessage(to, sf);
    setIntervalFromNow(this->packetTimeMax); // Delay start of sending payloads
}

/**
 * Returns the number of available packets in the message history for a specified destination node.
 *
 * @param dest The destination node number.
 * @param last_time The relative time to start counting messages from.
 * @return The number of available packets in the message history.
 */
uint32_t StoreForwardModule::getNumAvailablePackets(NodeNum dest, uint32_t last_time)
{
    uint32_t count = 0;
    if (lastRequest.find(dest) == lastRequest.end()) {
        lastRequest.emplace(dest, 0);
    }
    for (uint32_t i = lastRequest[dest]; i < this->packetHistoryTotalCount; i++) {
        if (this->packetHistory[i].time && (this->packetHistory[i].time > last_time)) {
            // Client is only interested in packets not from itself and only in broadcast packets or packets towards it.
            if (this->packetHistory[i].from != dest &&
                (this->packetHistory[i].to == NODENUM_BROADCAST || this->packetHistory[i].to == dest)) {
                count++;
            }
        }
    }
    return count;
}

/**
 * Allocates a mesh packet for sending to the phone.
 *
 * @return A pointer to the allocated mesh packet or nullptr if none is available.
 */
meshtastic_MeshPacket *StoreForwardModule::getForPhone()
{
    if (moduleConfig.store_forward.enabled && is_server) {
        NodeNum to = nodeDB->getNodeNum();
        if (!this->busy) {
            // Get number of packets we're going to send in this loop
            uint32_t histSize = getNumAvailablePackets(to, 0); // No time limit
            if (histSize) {
                this->busy = true;
                this->busyTo = to;
            } else {
                return nullptr;
            }
        }

        // We're busy with sending to us until no payload is available anymore
        if (this->busy && this->busyTo == to) {
            meshtastic_MeshPacket *p = preparePayload(to, 0, true); // No time limit
            if (!p)                                                 // No more messages to send
                this->busy = false;
            return p;
        }
    }
    return nullptr;
}

/**
 * Adds a mesh packet to the history buffer for store-and-forward functionality.
 *
 * @param mp The mesh packet to add to the history buffer.
 */
void StoreForwardModule::historyAdd(const meshtastic_MeshPacket &mp)
{
    const auto &p = mp.decoded;

    if (this->packetHistoryTotalCount == this->records) {
        LOG_WARN("S&F - PSRAM Full. Starting overwrite");
        this->packetHistoryTotalCount = 0;
        for (auto &i : lastRequest) {
            i.second = 0; // Clear the last request index for each client device
        }
    }

    this->packetHistory[this->packetHistoryTotalCount].time = getTime();
    this->packetHistory[this->packetHistoryTotalCount].to = mp.to;
    this->packetHistory[this->packetHistoryTotalCount].channel = mp.channel;
    this->packetHistory[this->packetHistoryTotalCount].from = getFrom(&mp);
    this->packetHistory[this->packetHistoryTotalCount].id = mp.id;
    this->packetHistory[this->packetHistoryTotalCount].reply_id = p.reply_id;
    this->packetHistory[this->packetHistoryTotalCount].emoji = (bool)p.emoji;
    this->packetHistory[this->packetHistoryTotalCount].payload_size = p.payload.size;
    this->packetHistory[this->packetHistoryTotalCount].rx_rssi = mp.rx_rssi;
    this->packetHistory[this->packetHistoryTotalCount].rx_snr = mp.rx_snr;
    memcpy(this->packetHistory[this->packetHistoryTotalCount].payload, p.payload.bytes, meshtastic_Constants_DATA_PAYLOAD_LEN);

    this->packetHistoryTotalCount++;
}

/**
 * Sends a payload to a specified destination node using the store and forward mechanism.
 *
 * @param dest The destination node number.
 * @param last_time The relative time to start sending messages from.
 * @return True if a packet was successfully sent, false otherwise.
 */
bool StoreForwardModule::sendPayload(NodeNum dest, uint32_t last_time)
{
    meshtastic_MeshPacket *p = preparePayload(dest, last_time);
    if (p) {
        LOG_INFO("Send S&F Payload");
        service->sendToMesh(p);
        this->requestCount++;
        return true;
    }
    return false;
}

/**
 * Prepares a payload to be sent to a specified destination node from the S&F packet history.
 *
 * @param dest The destination node number.
 * @param last_time The relative time to start sending messages from.
 * @return A pointer to the prepared mesh packet or nullptr if none is available.
 */
meshtastic_MeshPacket *StoreForwardModule::preparePayload(NodeNum dest, uint32_t last_time, bool local)
{
    for (uint32_t i = lastRequest[dest]; i < this->packetHistoryTotalCount; i++) {
        if (this->packetHistory[i].time && (this->packetHistory[i].time > last_time)) {
            /*  Copy the messages that were received by the server in the last msAgo
                to the packetHistoryTXQueue structure.
                Client not interested in packets from itself and only in broadcast packets or packets towards it. */
            if (this->packetHistory[i].from != dest &&
                (this->packetHistory[i].to == NODENUM_BROADCAST || this->packetHistory[i].to == dest)) {

                meshtastic_MeshPacket *p = allocDataPacket();

                p->to = local ? this->packetHistory[i].to : dest; // PhoneAPI can handle original `to`
                p->from = this->packetHistory[i].from;
                p->id = this->packetHistory[i].id;
                p->channel = this->packetHistory[i].channel;
                p->decoded.reply_id = this->packetHistory[i].reply_id;
                p->rx_time = this->packetHistory[i].time;
                p->decoded.emoji = (uint32_t)this->packetHistory[i].emoji;
                p->rx_rssi = this->packetHistory[i].rx_rssi;
                p->rx_snr = this->packetHistory[i].rx_snr;

                // Let's assume that if the server received the S&F request that the client is in range.
                //   TODO: Make this configurable.
                p->want_ack = false;

                if (local) { // PhoneAPI gets normal TEXT_MESSAGE_APP
                    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
                    memcpy(p->decoded.payload.bytes, this->packetHistory[i].payload, this->packetHistory[i].payload_size);
                    p->decoded.payload.size = this->packetHistory[i].payload_size;
                } else {
                    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
                    sf.which_variant = meshtastic_StoreAndForward_text_tag;
                    sf.variant.text.size = this->packetHistory[i].payload_size;
                    memcpy(sf.variant.text.bytes, this->packetHistory[i].payload, this->packetHistory[i].payload_size);
                    if (this->packetHistory[i].to == NODENUM_BROADCAST) {
                        sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_BROADCAST;
                    } else {
                        sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_TEXT_DIRECT;
                    }

                    p->decoded.payload.size = pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
                                                                 &meshtastic_StoreAndForward_msg, &sf);
                }

                lastRequest[dest] = i + 1; // Update the last request index for the client device

                return p;
            }
        }
    }
    return nullptr;
}

/**
 * Sends a message to a specified destination node using the store and forward protocol.
 *
 * @param dest The destination node number.
 * @param payload The message payload to be sent.
 */
void StoreForwardModule::sendMessage(NodeNum dest, const meshtastic_StoreAndForward &payload)
{
    meshtastic_MeshPacket *p = allocDataProtobuf(payload);

    p->to = dest;

    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;

    // Let's assume that if the server received the S&F request that the client is in range.
    //   TODO: Make this configurable.
    p->want_ack = false;
    p->decoded.want_response = false;

    service->sendToMesh(p);
}

/**
 * Sends a store-and-forward message to the specified destination node.
 *
 * @param dest The destination node number.
 * @param rr The store-and-forward request/response message to send.
 */
void StoreForwardModule::sendMessage(NodeNum dest, meshtastic_StoreAndForward_RequestResponse rr)
{
    // Craft an empty response, save some bytes in flash
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;
    sf.rr = rr;
    storeForwardModule->sendMessage(dest, sf);
}

/**
 * Sends a text message with an error (busy or channel not available) to the specified destination node.
 *
 * @param dest The destination node number.
 * @param want_response True if the original message requested a response, false otherwise.
 */
void StoreForwardModule::sendErrorTextMessage(NodeNum dest, bool want_response)
{
    meshtastic_MeshPacket *pr = allocDataPacket();
    pr->to = dest;
    pr->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    pr->want_ack = false;
    pr->decoded.want_response = false;
    pr->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    const char *str;
    if (this->busy) {
        str = "S&F - Busy. Try again shortly.";
    } else {
        str = "S&F not permitted on the public channel.";
    }
    LOG_WARN("%s", str);
    memcpy(pr->decoded.payload.bytes, str, strlen(str));
    pr->decoded.payload.size = strlen(str);
    if (want_response) {
        ignoreRequest = true; // This text message counts as response.
    }
    service->sendToMesh(pr);
}

/**
 * Sends statistics about the store and forward module to the specified node.
 *
 * @param to The node ID to send the statistics to.
 */
void StoreForwardModule::statsSend(uint32_t to)
{
    meshtastic_StoreAndForward sf = meshtastic_StoreAndForward_init_zero;

    sf.rr = meshtastic_StoreAndForward_RequestResponse_ROUTER_STATS;
    sf.which_variant = meshtastic_StoreAndForward_stats_tag;
    sf.variant.stats.messages_total = this->records;
    sf.variant.stats.messages_saved = this->packetHistoryTotalCount;
    sf.variant.stats.messages_max = this->records;
    sf.variant.stats.up_time = millis() / 1000;
    sf.variant.stats.requests = this->requests;
    sf.variant.stats.requests_history = this->requests_history;
    sf.variant.stats.heartbeat = this->heartbeat;
    sf.variant.stats.return_max = this->historyReturnMax;
    sf.variant.stats.return_window = this->historyReturnWindow;

    LOG_DEBUG("Send S&F Stats");
    storeForwardModule->sendMessage(to, sf);
}

/**
 * Handles a received mesh packet, potentially storing it for later forwarding.
 *
 * @param mp The received mesh packet.
 * @return A `ProcessMessage` indicating whether the packet was successfully handled.
 */
ProcessMessage StoreForwardModule::handleReceived(const meshtastic_MeshPacket &mp)
{
#if defined(ARCH_ESP32) || defined(ARCH_PORTDUINO)
    if (moduleConfig.store_forward.enabled) {

        if ((mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) && is_server) {
            auto &p = mp.decoded;
            if (isToUs(&mp) && (p.payload.bytes[0] == 'S') && (p.payload.bytes[1] == 'F') && (p.payload.bytes[2] == 0x00)) {
                LOG_DEBUG("Legacy Request to send");

                // Send the last 60 minutes of messages.
                if (this->busy || channels.isDefaultChannel(mp.channel)) {
                    sendErrorTextMessage(getFrom(&mp), mp.decoded.want_response);
                } else {
                    storeForwardModule->historySend(historyReturnWindow * 60, getFrom(&mp));
                }
            } else {
                storeForwardModule->historyAdd(mp);
                LOG_INFO("S&F stored. Message history contains %u records now", this->packetHistoryTotalCount);
            }
        } else if (!isFromUs(&mp) && mp.decoded.portnum == meshtastic_PortNum_STORE_FORWARD_APP) {
            auto &p = mp.decoded;
            meshtastic_StoreAndForward scratch;
            meshtastic_StoreAndForward *decoded = NULL;
            if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
                if (pb_decode_from_bytes(p.payload.bytes, p.payload.size, &meshtastic_StoreAndForward_msg, &scratch)) {
                    decoded = &scratch;
                } else {
                    LOG_ERROR("Error decoding proto module!");
                    // if we can't decode it, nobody can process it!
                    return ProcessMessage::STOP;
                }
                return handleReceivedProtobuf(mp, decoded) ? ProcessMessage::STOP : ProcessMessage::CONTINUE;
            }
        } // all others are irrelevant
    }

#endif

    return ProcessMessage::CONTINUE; // Let others look at this message also if they want
}

/**
 * Handles a received protobuf message for the Store and Forward module.
 *
 * @param mp The received MeshPacket to handle.
 * @param p A pointer to the StoreAndForward object.
 * @return True if the message was successfully handled, false otherwise.
 */
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
                LOG_ERROR("Client in ERROR or ABORT requested");
                this->requestCount = 0;
                this->busy = false;
            }
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_HISTORY:
        if (is_server) {
            requests_history++;
            LOG_INFO("Client Request to send HISTORY");
            // Send the last 60 minutes of messages.
            if (this->busy || channels.isDefaultChannel(mp.channel)) {
                sendErrorTextMessage(getFrom(&mp), mp.decoded.want_response);
            } else {
                if ((p->which_variant == meshtastic_StoreAndForward_history_tag) && (p->variant.history.window > 0)) {
                    // window is in minutes
                    storeForwardModule->historySend(p->variant.history.window * 60, getFrom(&mp));
                } else {
                    storeForwardModule->historySend(historyReturnWindow * 60, getFrom(&mp)); // defaults to 4 hours
                }
            }
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_PING:
        if (is_server) {
            // respond with a ROUTER PONG
            storeForwardModule->sendMessage(getFrom(&mp), meshtastic_StoreAndForward_RequestResponse_ROUTER_PONG);
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_PONG:
        if (is_server) {
            // NodeDB is already updated
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_CLIENT_STATS:
        if (is_server) {
            LOG_INFO("Client Request to send STATS");
            if (this->busy) {
                storeForwardModule->sendMessage(getFrom(&mp), meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY);
                LOG_INFO("S&F - Busy. Try again shortly");
            } else {
                storeForwardModule->statsSend(getFrom(&mp));
            }
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_ERROR:
    case meshtastic_StoreAndForward_RequestResponse_ROUTER_BUSY:
        if (is_client) {
            LOG_DEBUG("StoreAndForward_RequestResponse_ROUTER_BUSY");
            // retry in messages_saved * packetTimeMax ms
            retry_delay = millis() + getNumAvailablePackets(this->busyTo, this->last_time) * packetTimeMax *
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
            LOG_INFO("StoreAndForward Heartbeat received");
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_PING:
        if (is_client) {
            // respond with a CLIENT PONG
            storeForwardModule->sendMessage(getFrom(&mp), meshtastic_StoreAndForward_RequestResponse_CLIENT_PONG);
        }
        break;

    case meshtastic_StoreAndForward_RequestResponse_ROUTER_STATS:
        if (is_client) {
            LOG_DEBUG("Router Response STATS");
            // These fields only have informational purpose on a client. Fill them to consume later.
            if (p->which_variant == meshtastic_StoreAndForward_stats_tag) {
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
                LOG_INFO("Router Response HISTORY - Sending %d messages from last %d minutes",
                         p->variant.history.history_messages, this->historyReturnWindow);
            }
        }
        break;

    default:
        break; // no need to do anything
    }
    return false; // RoutingModule sends it to the phone
}

StoreForwardModule::StoreForwardModule()
    : concurrency::OSThread("StoreForward"),
      ProtobufModule("StoreForward", meshtastic_PortNum_STORE_FORWARD_APP, &meshtastic_StoreAndForward_msg)
{

#if defined(ARCH_ESP32) || defined(ARCH_PORTDUINO)

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
        if ((config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER || moduleConfig.store_forward.is_server)) {
            LOG_INFO("Init Store & Forward Module in Server mode");
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
                    else
                        this->heartbeat = false;

                    // Popupate PSRAM with our data structures.
                    this->populatePSRAM();
                    is_server = true;
                } else {
                    LOG_INFO(".");
                    LOG_INFO("S&F: not enough PSRAM free, Disable");
                }
            } else {
                LOG_INFO("S&F: device doesn't have PSRAM, Disable");
            }

            // Client
        } else {
            is_client = true;
            LOG_INFO("Init Store & Forward Module in Client mode");
        }
    } else {
        disable();
    }
#endif
}
