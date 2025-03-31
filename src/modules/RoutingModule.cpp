#include "RoutingModule.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

RoutingModule *routingModule;

bool RoutingModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_Routing *r)
{
    bool maybePKI = mp.which_payload_variant == meshtastic_MeshPacket_encrypted_tag && mp.channel == 0 && !isBroadcast(mp.to);
    // Beginning of logic whether to drop the packet based on Rebroadcast mode
    if (mp.which_payload_variant == meshtastic_MeshPacket_encrypted_tag &&
        (config.device.rebroadcast_mode == meshtastic_Config_DeviceConfig_RebroadcastMode_LOCAL_ONLY ||
         config.device.rebroadcast_mode == meshtastic_Config_DeviceConfig_RebroadcastMode_KNOWN_ONLY)) {
        if (!maybePKI)
            return false;
        if ((nodeDB->getMeshNode(mp.from) == NULL || !nodeDB->getMeshNode(mp.from)->has_user) &&
            (nodeDB->getMeshNode(mp.to) == NULL || !nodeDB->getMeshNode(mp.to)->has_user))
            return false;
    } else if (owner.is_licensed && nodeDB->getLicenseStatus(mp.from) == UserLicenseStatus::NotLicensed) {
        // Don't let licensed users to rebroadcast packets from unlicensed users
        // If we know they are in-fact unlicensed
        LOG_DEBUG("Packet from unlicensed user, ignoring packet");
        return false;
    }

    printPacket("Routing sniffing", &mp);
    router->sniffReceived(&mp, r);

    // FIXME - move this to a non promsicious PhoneAPI module?
    // Note: we are careful not to send back packets that started with the phone back to the phone
    if ((isBroadcast(mp.to) || isToUs(&mp)) && (mp.from != 0)) {
        printPacket("Delivering rx packet", &mp);
        service->handleFromRadio(&mp);
    }

    return false; // Let others look at this message also if they want
}

meshtastic_MeshPacket *RoutingModule::allocReply()
{
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER)
        return NULL;
    assert(currentRequest);

    return NULL;
}

void RoutingModule::sendAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex, uint8_t hopLimit)
{
    auto p = allocAckNak(err, to, idFrom, chIndex, hopLimit);

    router->sendLocal(p); // we sometimes send directly to the local node
}

uint8_t RoutingModule::getHopLimitForResponse(uint8_t hopStart, uint8_t hopLimit)
{
    if (hopStart != 0) {
        // Hops used by the request. If somebody in between running modified firmware modified it, ignore it
        uint8_t hopsUsed = hopStart < hopLimit ? config.lora.hop_limit : hopStart - hopLimit;
        if (hopsUsed > config.lora.hop_limit) {
// In event mode, we never want to send packets with more than our default 3 hops.
#if !(EVENTMODE)             // This falls through to the default.
            return hopsUsed; // If the request used more hops than the limit, use the same amount of hops
#endif
        } else if ((uint8_t)(hopsUsed + 2) < config.lora.hop_limit) {
            return hopsUsed + 2; // Use only the amount of hops needed with some margin as the way back may be different
        }
    }
    return Default::getConfiguredOrDefaultHopLimit(config.lora.hop_limit); // Use the default hop limit
}

RoutingModule::RoutingModule() : ProtobufModule("routing", meshtastic_PortNum_ROUTING_APP, &meshtastic_Routing_msg)
{
    isPromiscuous = true;

    // moved the RebroadcastMode logic into handleReceivedProtobuf
    // LocalOnly requires either the from or to to be a known node
    // knownOnly specifically requires the from to be a known node.
    encryptedOk = true;
}