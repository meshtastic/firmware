#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

#pragma once
#include "NodeDB.h"

class TypeConversions
{
  public:
    // Either pointer may be null; the corresponding has_* fields stay unset.
    static meshtastic_NodeInfo ConvertToNodeInfo(const meshtastic_NodeInfoLite *lite, const meshtastic_PositionLite *position,
                                                 const meshtastic_DeviceMetrics *deviceMetrics);
    static meshtastic_NodeInfo ConvertToNodeInfo(const meshtastic_NodeInfoLite *lite);
    // Identity + link-state only; satellite payloads are replayed afterward.
    static meshtastic_NodeInfo ConvertToNodeInfoThin(const meshtastic_NodeInfoLite *lite);

    static meshtastic_PositionLite ConvertToPositionLite(meshtastic_Position position);
    static meshtastic_Position ConvertToPosition(meshtastic_PositionLite lite);

    static void CopyUserToNodeInfoLite(meshtastic_NodeInfoLite *lite, const meshtastic_User &user);
    static meshtastic_User ConvertToUser(const meshtastic_NodeInfoLite *lite);
};
