#pragma once

#include "SinglePortPlugin.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

class RangeTestPlugin : private concurrency::OSThread
{
    bool firstTime = 1;

  public:
    RangeTestPlugin();

  protected:
    virtual int32_t runOnce();
};

extern RangeTestPlugin *rangeTestPlugin;

/*
 * Radio interface for RangeTestPlugin
 *
 */
class RangeTestPluginRadio : public SinglePortPlugin
{
    uint32_t lastRxID;

  public:

    RangeTestPluginRadio() : SinglePortPlugin("RangeTestPluginRadio", PortNum_TEXT_MESSAGE_APP) {}

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  protected:
    virtual MeshPacket *allocReply();

    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceived(const MeshPacket &mp);
};

extern RangeTestPluginRadio *rangeTestPluginRadio;