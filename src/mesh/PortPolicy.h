#pragma once

// Per-port policy for the packets this node sends on request or on a timer: position, telemetry,
// paxcounter and neighbour info. Each port keeps a `policy_flags` (PortPolicyFlags) in its own config
// message next to its routine destination, and the helpers here are the only readers. Every bit
// restricts, so 0 is exactly the behaviour with no policy.
//
// A packet with no destination is a broadcast: channel PSK, read by every member. A packet with a
// destination is either a unicast (PKI to that node's key, readable by it alone) or a directed
// broadcast (addressed to one node, channel PSK, so every member can read it and only the addressee
// acts on it). Bits 0-3 say who may pull the packet with a want_response request; bits 4-7 say
// whether a packet with a destination goes as a unicast or a directed broadcast.
//
// The reply bits run in the module's allocReply() before any throttle, so a refused request costs no
// airtime and no "no response" NAK (ignoreRequest). Under PKC_ALWAYS a poller whose key we do not
// hold is refused there too: the reply could only be built and then fail at encode. The crypto bits
// run in the Router (wouldEncryptWithPKC) for every from-us packet with a destination on the port:
// routine sends, replies to pollers and phone-originated sends alike. Never for a packet we relay -
// that carries its sender's policy, not ours.
//
// The policy is per port, not per payload: every telemetry sub-type shares TelemetryConfig.policy_flags
// because they all ride TELEMETRY_APP and the Router never decodes payloads. Never read one port's
// flags from another port's config. Destinations are the exception: each telemetry sub-type keeps its
// own, so REPLY_ONLY_TO_DEST admits the device-metrics destination to device metrics alone.
// REPLY_ONLY_TO_DEST compares against the destination for that packet, so a sub-type with no or an
// unset destination admits nobody on the mesh, on purpose. The
// phone reaches its node as a request from our own node number and is admitted before any bit is
// read; ignored nodes are refused before any bit is read. PKC_ALWAYS with PKC_NEVER resolves to
// PKC_ALWAYS. The Router never unicasts POSITION_APP unless PKC_ALWAYS is set on the position port,
// and then the precision comes from the channel the destination's NodeInfo last arrived on
// (NodeDB::updateUser), which is the primary channel for most contacts
// (PositionModule::directedSendChannel).
// With neither crypto bit set, a routine send to the port's own destination, or a reply to whoever
// polled us, goes as a directed broadcast when the key is not held, rather than going nowhere; a
// unicast the client composed keeps the PKI refusal, since the client asked for that node by key.
// That fallback is for these metric ports only, not a pattern for text or admin.

#include "NodeDB.h"
#include "mesh-pb-constants.h"

/// The PortPolicyFlags for a port that carries one, else false. Position, telemetry, paxcounter
/// and neighbour info each keep theirs next to that port's routine destination.
static inline bool portPolicyFlags(meshtastic_PortNum port, uint32_t &flags)
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

/// Can this node use PKI at all? PKC_ALWAYS cannot be honoured without it, whatever key we hold for
/// the far end: the encoder needs our own private key, and licensed mode does not encrypt.
static inline bool localPkiUsable()
{
#if MESHTASTIC_EXCLUDE_PKI
    return false;
#else
    return config.security.private_key.size == 32 && !owner.is_licensed;
#endif
}

/// Is `to` a routine destination configured for `port`? Telemetry's sub-types share one port, so any
/// of the five counts. Used to tell our own routine traffic from a unicast the client composed.
static inline bool isRoutineDest(meshtastic_PortNum port, NodeNum to)
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

/// May we answer a want_response request from `from` under `flags`? Ignored nodes are refused
/// regardless; every bit only restricts. `dest` is the port's routine destination, 0 when none.
static inline bool replyPolicyAllows(uint32_t flags, NodeNum from, NodeNum dest)
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

/// Admin gate: PKC_ALWAYS means unicast only, so every routine destination must have a public key
/// in NodeDB or the routine send fails at encode on every interval. `why`, when given, receives the
/// refusal text for the caller to put in front of the user; the header stays clear of MeshService.
static inline bool pkcAlwaysDestsHaveKeys(uint32_t flags, const uint32_t *dests, size_t count, char *why = nullptr,
                                          size_t whyLen = 0)
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
        LOG_WARN("PKC_ALWAYS refused: %s", lack);
        if (why && whyLen)
            snprintf(why, whyLen, "PKC_ALWAYS refused: %s", lack);
        return false;
    }
#if !MESHTASTIC_EXCLUDE_PKI
    for (size_t i = 0; i < count; i++) {
        meshtastic_NodeInfoLite_public_key_t key;
        if (dests[i] && !(nodeDB && nodeDB->copyPublicKey(dests[i], key))) {
            LOG_WARN("PKC_ALWAYS refused: no public key for destination 0x%08x", dests[i]);
            if (why && whyLen)
                snprintf(why, whyLen, "PKC_ALWAYS refused: no public key for destination !%08x", dests[i]);
            return false;
        }
    }
#endif
    return true;
}
