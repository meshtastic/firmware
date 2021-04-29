#pragma once

#include "SinglePortPlugin.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

class TunnelPlugin : private concurrency::OSThread
{
    bool firstTime = 1;

  public:
    TunnelPlugin();

  protected:
    virtual int32_t runOnce();
};

extern TunnelPlugin *tunnelPlugin;

/*
 * Radio interface for SerialPlugin
 *
 */
class TunnelPluginRadio : public SinglePortPlugin
{
    uint32_t lastRxID;

  public:
    /*
        TODO: Switch this to PortNum_SERIAL_APP once the change is able to be merged back here
              from the main code.
    */

    TunnelPluginRadio();

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

    virtual bool wantUIFrame() { return false; }
};

extern TunnelPluginRadio *tunnelPluginRadio;