#pragma once
#include "ProtobufModule.h"
#include "concurrency/OSThread.h"
#include "mesh/generated/meshtastic/powermon.pb.h"

/**
 * A module that provides easy low-level remote access to device hardware.
 */
class PowerStressModule : public ProtobufModule<meshtastic_PowerStressMessage>, private concurrency::OSThread
{
    meshtastic_PowerStressMessage currentMessage = meshtastic_PowerStressMessage_init_default;
    bool isRunningCommand = false;

  public:
    /** Constructor
     * name is for debugging output
     */
    PowerStressModule();

  protected:
    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_PowerStressMessage *p) override;

    /**
     * Periodically read the gpios we have been asked to WATCH, if they have changed,
     * broadcast a message with the change information.
     *
     * The method that will be called each time our thread gets a chance to run
     *
     * Returns desired period for next invocation (or RUN_SAME for no change)
     */
    virtual int32_t runOnce() override;
};

extern PowerStressModule powerStressModule;