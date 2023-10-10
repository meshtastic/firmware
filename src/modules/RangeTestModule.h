#pragma once

#include "SinglePortModule.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

class RangeTestModule : private concurrency::OSThread
{
    bool firstTime = 1;
    unsigned long started = 0;

  public:
    RangeTestModule();

  protected:
    virtual int32_t runOnce() override;
};

extern RangeTestModule *rangeTestModule;

/*
 * Radio interface for RangeTestModule
 *
 */
class RangeTestModuleRadio : public SinglePortModule
{
    uint32_t lastRxID = 0;

  public:
    RangeTestModuleRadio() : SinglePortModule("RangeTestModuleRadio", meshtastic_PortNum_RANGE_TEST_APP)
    {
        loopbackOk = true; // Allow locally generated messages to loop back to the client
    }

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

    /**
     * Append range test data to the file on the Filesystem
     */
    bool appendFile(const meshtastic_MeshPacket &mp);

  protected:
    /** Called to handle a particular incoming message

    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
    it
    */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
};

extern RangeTestModuleRadio *rangeTestModuleRadio;
