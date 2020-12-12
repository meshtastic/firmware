#pragma once
#include "ProtobufPlugin.h"
#include "remote_hardware.pb.h"

/**
 * A plugin that provides easy low-level remote access to device hardware.
 */
class RemoteHardwarePlugin : public ProtobufPlugin<HardwareMessage>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    RemoteHardwarePlugin() : ProtobufPlugin("remotehardware", PortNum_REMOTE_HARDWARE_APP, HardwareMessage_fields) {}

  protected:
    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, const HardwareMessage &p);
};

extern RemoteHardwarePlugin remoteHardwarePlugin;