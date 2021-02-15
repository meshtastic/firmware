#pragma once

#include "SinglePortPlugin.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

class StoreForwardPlugin : private concurrency::OSThread
{
    bool firstTime = 1;

  public:
    StoreForwardPlugin();

  protected:
    virtual int32_t runOnce();
};

extern StoreForwardPlugin *storeForwardPlugin;

/*
 * Radio interface for StoreForwardPlugin
 *
 */
class StoreForwardPluginRadio : public SinglePortPlugin
{
    //uint32_t lastRxID;

  public:
    StoreForwardPluginRadio() : SinglePortPlugin("StoreForwardPluginRadio", PortNum_STORE_FORWARD_APP) {}
    //StoreForwardPluginRadio() : SinglePortPlugin("StoreForwardPluginRadio", PortNum_TEXT_MESSAGE_APP) {}

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  protected:
    virtual MeshPacket *allocReply();

    virtual bool wantPortnum(PortNum p){return true;};

    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceived(const MeshPacket &mp);
};

extern StoreForwardPluginRadio *storeForwardPluginRadio;

