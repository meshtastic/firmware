#include "configuration.h"
#include "PositionPlugin.h"
#include "NodeDB.h"

PositionPlugin positionPlugin;

bool PositionPlugin::handleReceived(const MeshPacket &mp)
{
    auto &p = mp.decoded.data;
    DEBUG_MSG("Received position from=0x%0x, id=%d, msg=%.*s\n", mp.from, mp.id, p.payload.size, p.payload.bytes);

    return false; // Let others look at this message also if they want
}
