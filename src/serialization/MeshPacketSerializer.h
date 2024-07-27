#include <meshtastic/mesh.pb.h>
#include <string>

class MeshPacketSerializer
{
  public:
    static std::string JsonSerialize(meshtastic_MeshPacket *mp, bool shouldLog = true);
};