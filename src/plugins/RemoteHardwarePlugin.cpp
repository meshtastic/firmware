#include "RemoteHardwarePlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

RemoteHardwarePlugin remoteHardwarePlugin;

bool RemoteHardwarePlugin::handleReceivedProtobuf(const MeshPacket &mp, const HardwareMessage &p)
{

    return false; // Let others look at this message also if they want
}


