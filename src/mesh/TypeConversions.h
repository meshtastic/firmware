#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

#pragma once
#include "NodeDB.h"

class TypeConversions
{
  public:
    static meshtastic_NodeInfo ConvertToNodeInfo(const meshtastic_NodeInfoLite *lite);
    static meshtastic_PositionLite ConvertToPositionLite(meshtastic_Position position);
    static meshtastic_Position ConvertToPosition(meshtastic_PositionLite lite);
    static meshtastic_UserLite ConvertToUserLite(meshtastic_User user);
    static meshtastic_User ConvertToUser(uint32_t nodeNum, meshtastic_UserLite lite);
};
