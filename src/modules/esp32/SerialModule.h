#pragma once

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

class SerialModule : private concurrency::OSThread
{
    bool firstTime = 1;

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
class SerialModuleRadio : public SinglePortModule
{
    uint32_t lastRxID = 0;

  public:
    /*
        TODO: Switch this to PortNum_SERIAL_APP once the change is able to be merged back here
              from the main code.
    */

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
};

extern SerialModuleRadio *serialModuleRadio;
