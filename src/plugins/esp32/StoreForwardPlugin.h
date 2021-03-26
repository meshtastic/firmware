#pragma once

#include "SinglePortPlugin.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

struct PacketHistoryStruct {
    uint32_t time;
    uint32_t to;
    bool ack;
    uint8_t bytes[MAX_RHPACKETLEN];
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
    void sawNode(uint32_t whoWeSaw, uint32_t sawSecAgo);
    void historyAdd(const MeshPacket *mp);
    void historyReport();
    void historySend(uint32_t msAgo, uint32_t to);
    void populatePSRAM();

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);
    void sendPayloadWelcome(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);
    virtual MeshPacket *allocReply();
    virtual bool wantPortnum(PortNum p) { return true; };

  private:
    // Nothing here

  protected:
    virtual int32_t runOnce();

    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceived(const MeshPacket &mp);
};

extern StoreForwardPlugin *storeForwardPlugin;

/*
 * Radio interface for StoreForwardPlugin
 *
 */

/*
class StoreForwardPluginRadio : public SinglePortPlugin
{
    // uint32_t lastRxID;

  public:
    StoreForwardPluginRadio() : SinglePortPlugin("StoreForwardPluginRadio", PortNum_STORE_FORWARD_APP) {}
    // StoreForwardPluginRadio() : SinglePortPlugin("StoreForwardPluginRadio", PortNum_TEXT_MESSAGE_APP) {}

    void sendPayloadHeartbeat(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  protected:
    virtual MeshPacket *allocReply2();
};

extern StoreForwardPluginRadio *storeForwardPluginRadio;
*/
