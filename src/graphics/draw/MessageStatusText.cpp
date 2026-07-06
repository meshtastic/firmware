#include "configuration.h"

#if HAS_SCREEN || defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS)
#include "MeshTypes.h"
#include "MessageStatusText.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

namespace graphics
{
namespace MessageStatusText
{

const char *inlineTextFor(const StoredMessage &message)
{
    switch (message.ackStatus) {
    case AckStatus::NONE:
        return "Sending...";
    case AckStatus::ACKED:
        return message.dest == NODENUM_BROADCAST ? "Delivered to mesh" : "Delivered to recipient";
    case AckStatus::RELAYED:
        return message.dest == NODENUM_BROADCAST ? "Delivered to mesh" : "Relayed, not confirmed by recipient";
    case AckStatus::TOO_LARGE:
        return "Message is too large to send";
    case AckStatus::NACKED:
    case AckStatus::TIMEOUT:
        return "Failed to deliver to mesh";
    }

    return "";
}

} // namespace MessageStatusText
} // namespace graphics
#endif
