#pragma once

#include "ProtobufModule.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/storeforward.pb.h"

#include "configuration.h"
#include <Arduino.h>
#include <functional>

struct PacketHistoryStruct {
    uint32_t time;
    uint32_t to;
    uint32_t from;
    uint8_t channel;
    bool ack;
    uint8_t payload[meshtastic_Constants_DATA_PAYLOAD_LEN];
    pb_size_t payload_size;
};

class StoreForwardModule : private concurrency::OSThread, public ProtobufModule<meshtastic_StoreAndForward>
{
    bool busy = 0;
    uint32_t busyTo = 0;
    char routerMessage[meshtastic_Constants_DATA_PAYLOAD_LEN] = {0};

    PacketHistoryStruct *packetHistory = 0;
    uint32_t packetHistoryCurrent = 0;
    uint32_t packetHistoryMax = 0;

    PacketHistoryStruct *packetHistoryTXQueue = 0;
    uint32_t packetHistoryTXQueue_size = 0;
    uint32_t packetHistoryTXQueue_index = 0;

    uint32_t packetTimeMax = 5000;

    bool is_client = false;
    bool is_server = false;

  public:
    StoreForwardModule();

    unsigned long lastHeartbeat = 0;
    uint32_t heartbeatInterval = 300;

    /**
     Update our local reference of when we last saw that node.
     @return 0 if we have never seen that node before otherwise return the last time we saw the node.
     */
    void historyAdd(const meshtastic_MeshPacket &mp);
    void statsSend(uint32_t to);
    void historySend(uint32_t msAgo, uint32_t to);

    uint32_t historyQueueCreate(uint32_t msAgo, uint32_t to);

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, uint32_t packetHistory_index = 0);
    void sendMessage(NodeNum dest, const meshtastic_StoreAndForward &payload);
    void sendMessage(NodeNum dest, meshtastic_StoreAndForward_RequestResponse rr);

    virtual meshtastic_MeshPacket *allocReply() override;
    /*
      -Override the wantPacket method.
    */
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        switch (p->decoded.portnum) {
        case meshtastic_PortNum_TEXT_MESSAGE_APP:
        case meshtastic_PortNum_STORE_FORWARD_APP:
            return true;
        default:
            return false;
        }
    }

  private:
    void populatePSRAM();

    // S&F Defaults
    uint32_t historyReturnMax = 250;    // 250 records
    uint32_t historyReturnWindow = 240; // 4 hours
    uint32_t records = 0;               // Calculated
    bool heartbeat = false;             // No heartbeat.

    // stats
    uint32_t requests = 0;
    uint32_t requests_history = 0;

    uint32_t retry_delay = 0;

  protected:
    virtual int32_t runOnce() override;

    /** Called to handle a particular incoming message

    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
    it
    */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_StoreAndForward *p);
};

extern StoreForwardModule *storeForwardModule;
