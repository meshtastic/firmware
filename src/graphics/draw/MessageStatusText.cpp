#include "configuration.h"

#if HAS_SCREEN || defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS) || defined(MESHTASTIC_INCLUDE_BASE_UI_MESSAGE_STATUS)
#include "MeshTypes.h"
#include "MessageStatusText.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

namespace graphics
{
namespace MessageStatusText
{

bool isFailureStatus(AckStatus status)
{
    switch (status) {
    case AckStatus::NACKED:
    case AckStatus::TIMEOUT:
    case AckStatus::TOO_LARGE:
    case AckStatus::NO_CHANNEL:
    case AckStatus::PKI_FAILED:
    case AckStatus::PKI_UNKNOWN_PUBKEY:
    case AckStatus::PKI_SEND_FAIL_PUBLIC_KEY:
    case AckStatus::NO_INTERFACE:
    case AckStatus::DUTY_CYCLE_LIMIT:
    case AckStatus::RATE_LIMIT_EXCEEDED:
    case AckStatus::NO_RESPONSE:
    case AckStatus::BAD_REQUEST:
    case AckStatus::NOT_AUTHORIZED:
    case AckStatus::ADMIN_BAD_SESSION_KEY:
    case AckStatus::ADMIN_PUBLIC_KEY_UNAUTHORIZED:
        return true;
    case AckStatus::NONE:
    case AckStatus::ACKED:
    case AckStatus::RELAYED:
        return false;
    }

    return false;
}

const char *inlineTextFor(const StoredMessage &message)
{
    switch (message.ackStatus) {
    case AckStatus::NONE:
        return message.ackTrackable ? "Sending..." : "";
    case AckStatus::ACKED:
        return isBroadcast(message.dest) ? "Delivered to mesh" : "Delivered to recipient";
    case AckStatus::RELAYED:
        return isBroadcast(message.dest) ? "Delivered to mesh" : "Relayed, not confirmed by recipient";
    case AckStatus::TOO_LARGE:
        return "Message is too large to send";
    case AckStatus::NO_CHANNEL:
        return "Channel/key mismatch";
    case AckStatus::NO_INTERFACE:
        return "No radio interface";
    case AckStatus::DUTY_CYCLE_LIMIT:
        return "Duty cycle limit";
    case AckStatus::RATE_LIMIT_EXCEEDED:
        return "Rate limited";
    case AckStatus::NO_RESPONSE:
        return "No app response";
    case AckStatus::BAD_REQUEST:
        return "Invalid request";
    case AckStatus::NOT_AUTHORIZED:
        return "Not authorized";
    case AckStatus::ADMIN_BAD_SESSION_KEY:
        return "Admin session expired";
    case AckStatus::ADMIN_PUBLIC_KEY_UNAUTHORIZED:
        return "Admin key not authorized";
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
        return isBroadcast(message.dest) ? "Delivered to mesh" : "Relayed, not confirmed\nby recipient";
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
