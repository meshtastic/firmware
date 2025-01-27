#include <meshtastic/mesh.pb.h>
#include <string>

static const char hexChars[16] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};

class MeshPacketSerializer
{
  public:
    static std::string JsonSerialize(const meshtastic_MeshPacket *mp, bool shouldLog = true);
    static std::string JsonSerializeEncrypted(const meshtastic_MeshPacket *mp);

  private:
    static std::string bytesToHex(const uint8_t *bytes, int len)
    {
        std::string result = "";
        for (int i = 0; i < len; ++i) {
            char const byte = bytes[i];
            result += hexChars[(byte & 0xF0) >> 4];
            result += hexChars[(byte & 0x0F) >> 0];
        }
        return result;
    }
};