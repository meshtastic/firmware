#pragma once
#include "ProtobufModule.h"
#include "mesh/generated/remote_hardware.pb.h"
#include "concurrency/OSThread.h"

/**
 * A module that provides easy low-level remote access to device hardware.
 */
class RemoteHardwareModule : public ProtobufModule<HardwareMessage>, private concurrency::OSThread
{
    /// The current set of GPIOs we've been asked to watch for changes
    uint64_t watchGpios = 0; 

    /// The previously read value of watched pins
    uint64_t previousWatch = 0;

    /// The timestamp of our last watch event (we throttle watches to 1 change every 30 seconds)
    uint32_t lastWatchMsec = 0;
  public:
    /** Constructor
     * name is for debugging output
     */
    RemoteHardwareModule();

  protected:
    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, HardwareMessage *p) override;

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

extern RemoteHardwareModule remoteHardwareModule;
