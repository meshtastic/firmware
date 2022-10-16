#pragma once

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include "MeshModule.h"
#include "Router.h"
#include <functional>

class SerialModule : private concurrency::OSThread
{
    bool firstTime = 1;
    unsigned long lastNmeaTime = millis();
    char outbuf[90] = "";

  public:
    SerialModule();

  protected:
    virtual int32_t runOnce() override;
};

extern SerialModule *serialModule;

/*
 * Radio interface for SerialModule
 *
 */
class SerialModuleRadio : public MeshModule
{
    uint32_t lastRxID = 0;
    char outbuf[90] = "";

  public:
    SerialModuleRadio();

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

  protected:
    virtual MeshPacket *allocReply() override;

    /** Called to handle a particular incoming message

    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual ProcessMessage handleReceived(const MeshPacket &mp) override;

    PortNum ourPortNum;

    virtual bool wantPacket(const MeshPacket *p) override { return p->decoded.portnum == ourPortNum; }

    MeshPacket *allocDataPacket()
    {
        // Update our local node info with our position (even if we don't decide to update anyone else)
        MeshPacket *p = router->allocForSending();
        p->decoded.portnum = ourPortNum;

        return p;
    }

};

extern SerialModuleRadio *serialModuleRadio;
