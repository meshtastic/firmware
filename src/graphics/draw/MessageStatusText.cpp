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
        return message.ackTrackable ? "Sending..." : "";
    case AckStatus::ACKED:
        return message.dest == NODENUM_BROADCAST ? "Delivered to mesh" : "Delivered to recipient";
    case AckStatus::RELAYED:
        return message.dest == NODENUM_BROADCAST ? "Delivered to mesh" : "Relayed, not confirmed by recipient";
    case AckStatus::TOO_LARGE:
        return "Message is too large to send";
    case AckStatus::NO_CHANNEL:
        return "No channel selected";
    case AckStatus::PKI_FAILED:
        return "Could not send encrypted message";
    case AckStatus::PKI_UNKNOWN_PUBKEY:
        return "Recipient needs your key";
    case AckStatus::PKI_SEND_FAIL_PUBLIC_KEY:
        return "Recipient key unavailable";
    case AckStatus::NACKED:
    case AckStatus::TIMEOUT:
        return "Failed to deliver to mesh";
    }

    return "";
}

const char *bannerTextFor(const StoredMessage &message)
{
    switch (message.ackStatus) {
    case AckStatus::RELAYED:
        return message.dest == NODENUM_BROADCAST ? "Delivered to mesh" : "Relayed, not confirmed\nby recipient";
    case AckStatus::TOO_LARGE:
        return "Message is too large\nto send";
    case AckStatus::PKI_FAILED:
        return "Could not send\nencrypted message";
    case AckStatus::PKI_UNKNOWN_PUBKEY:
        return "Recipient needs\nyour key";
    case AckStatus::PKI_SEND_FAIL_PUBLIC_KEY:
        return "Recipient key\nunavailable";
    default:
        return inlineTextFor(message);
    }
}

} // namespace MessageStatusText
} // namespace graphics
#endif
