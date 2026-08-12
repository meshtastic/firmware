// Stub for MeshPacketSerializer: the real impl (serialization/MeshPacketSerializer.cpp)
// needs jsoncpp (<json/json.h>) and is only used for MQTT JSON output, which the
// wasm build excludes. Router.cpp still references these symbols, so provide
// empty implementations to satisfy the link without pulling in jsoncpp.
#include "serialization/MeshPacketSerializer.h"

std::string MeshPacketSerializer::JsonSerialize(const meshtastic_MeshPacket *mp, bool shouldLog)
{
    (void)mp;
    (void)shouldLog;
    return "";
}

std::string MeshPacketSerializer::JsonSerializeEncrypted(const meshtastic_MeshPacket *mp)
{
    (void)mp;
    return "";
}
