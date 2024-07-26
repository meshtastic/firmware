#include <meshtastic/mesh.pb.h>
#include <string>

class MeshPacketSerializer
{
  public:
    static std::string JsonSerialize(const meshtastic_MeshPacket *mp, bool shouldLog = true);
    static std::string JsonSerializeEncrypted(const meshtastic_MeshPacket *mp);
};