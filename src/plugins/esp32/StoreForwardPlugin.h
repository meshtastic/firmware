#pragma once

#include "SinglePortPlugin.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

struct PacketHistoryStruct {
    uint32_t time;
    uint32_t to;
    uint32_t from;
    bool ack;
    uint8_t payload[Constants_DATA_PAYLOAD_LEN];
    pb_size_t payload_size;
};

class StoreForwardPlugin : public SinglePortPlugin, private concurrency::OSThread
{
    bool firstTime = 1;

    uint32_t receivedRecord[50][2] = {{0}};

    PacketHistoryStruct *packetHistory;
    uint32_t packetHistoryCurrent = 0;

  public:
    StoreForwardPlugin();

    /**
     Update our local reference of when we last saw that node.
     @return 0 if we have never seen that node before otherwise return the last time we saw the node.
     */
    void historyAdd(const MeshPacket &mp);
    void historyReport();
    void historySend(uint32_t msAgo, uint32_t to);
    void populatePSRAM();

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, uint32_t packetHistory_index = 0);
    virtual MeshPacket *allocReply();
    virtual bool wantPortnum(PortNum p) { return true; };

  private:
  // Nothing here.

  protected:
    virtual int32_t runOnce();

    /** Called to handle a particular incoming message

    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
    it
    */
    virtual ProcessMessage handleReceived(const MeshPacket &mp);
};

extern StoreForwardPlugin *storeForwardPlugin;