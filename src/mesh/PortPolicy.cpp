#include "PortPolicy.h"

bool portPolicyFlags(meshtastic_PortNum port, uint32_t &flags)
{
    switch (port) {
    case meshtastic_PortNum_POSITION_APP:
        flags = config.position.policy_flags;
        return true;
    case meshtastic_PortNum_TELEMETRY_APP:
        flags = moduleConfig.telemetry.policy_flags;
        return true;
    case meshtastic_PortNum_PAXCOUNTER_APP:
        flags = moduleConfig.paxcounter.policy_flags;
        return true;
    case meshtastic_PortNum_NEIGHBORINFO_APP:
        flags = moduleConfig.neighbor_info.policy_flags;
        return true;
    default:
        return false;
    }
}

bool localPkiUsable()
{
#if MESHTASTIC_EXCLUDE_PKI
    return false;
#else
    return config.security.private_key.size == 32 && !owner.is_licensed;
#endif
}

bool isRoutineDest(meshtastic_PortNum port, NodeNum to)
{
    if (!to || isBroadcast(to))
        return false;
    switch (port) {
    case meshtastic_PortNum_POSITION_APP:
        return config.position.position_dest == to;
    case meshtastic_PortNum_TELEMETRY_APP:
        return moduleConfig.telemetry.device_dest == to || moduleConfig.telemetry.environment_dest == to ||
               moduleConfig.telemetry.air_quality_dest == to || moduleConfig.telemetry.power_dest == to ||
               moduleConfig.telemetry.health_dest == to;
    case meshtastic_PortNum_PAXCOUNTER_APP:
        return moduleConfig.paxcounter.paxcounter_dest == to;
    default:
        return false; // neighbour info has no destination: only its replies fall back
    }
}

bool replyPolicyAllows(uint32_t flags, NodeNum from, NodeNum dest)
{
    // The flags govern who on the mesh may poll us; a request from our own node is the phone.
    if (nodeDB && from == nodeDB->getNodeNum())
        return true;
    if (nodeDB) {
        const meshtastic_NodeInfoLite *n = nodeDB->getMeshNode(from);
        if (n && nodeInfoLiteIsIgnored(n))
            return false;
    }
    if (flags & meshtastic_PortPolicyFlags_NO_ADHOC_REPLY)
        return false;
    // PKC_ALWAYS means the reply can only go encrypted to that node. Without an identity of our own,
    // or without its key, the reply would be built and then refused at encode, costing the phone a
    // NAK per poll; refuse it here instead.
    if (flags & meshtastic_PortPolicyFlags_PKC_ALWAYS) {
        if (!localPkiUsable())
            return false;
#if !MESHTASTIC_EXCLUDE_PKI
        meshtastic_NodeInfoLite_public_key_t key;
        if (!nodeDB || !nodeDB->copyPublicKey(from, key))
            return false;
#endif
    }
    if ((flags & meshtastic_PortPolicyFlags_REPLY_ONLY_TO_DEST) && (!dest || from != dest))
        return false;
    if ((flags & meshtastic_PortPolicyFlags_REPLY_TO_FAVOURITES_ONLY) && nodeDB && !nodeDB->isFavorite(from))
        return false;
    return true;
}

// One refusal text for the log and for the admin caller.
static bool pkcAlwaysRefused(char *why, size_t whyLen, const char *text)
{
    LOG_WARN("PKC_ALWAYS refused: %s", text);
    if (why && whyLen)
        snprintf(why, whyLen, "PKC_ALWAYS refused: %s", text);
    return false;
}

bool pkcAlwaysDestsHaveKeys(uint32_t flags, const uint32_t *dests, size_t count, char *why, size_t whyLen)
{
    if (!(flags & meshtastic_PortPolicyFlags_PKC_ALWAYS))
        return true;
    // Destination keys arrive by NodeInfo whether or not this node can use them, so checking them
    // alone would accept a setting that then fails at encode on every interval.
    if (!localPkiUsable()) {
        (void)dests;
        (void)count;
#if MESHTASTIC_EXCLUDE_PKI
        const char *lack = "this firmware build does not include PKI";
#else
        const char *lack = owner.is_licensed ? "licensed mode does not encrypt" : "this node has no PKI identity";
#endif
        return pkcAlwaysRefused(why, whyLen, lack);
    }
#if !MESHTASTIC_EXCLUDE_PKI
    for (size_t i = 0; i < count; i++) {
        meshtastic_NodeInfoLite_public_key_t key;
        if (dests[i] && !(nodeDB && nodeDB->copyPublicKey(dests[i], key))) {
            char text[48];
            snprintf(text, sizeof(text), "no public key for destination !%08x", dests[i]);
            return pkcAlwaysRefused(why, whyLen, text);
        }
    }
#endif
    return true;
}
