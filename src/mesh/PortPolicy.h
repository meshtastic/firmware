#pragma once

// Per-port policy for the packets this node sends on request or on a timer: position, telemetry,
// paxcounter and neighbour info. Each of those ports keeps a `policy_flags` (a PortPolicyFlags
// bitfield) in its own config message, next to that port's routine destination, and the helpers
// here are the only readers. Every bit restricts, so 0 is exactly the behaviour with no policy.
//
// Terminology. A packet with no destination is a broadcast: channel PSK, read by every member. A
// packet with a destination is either a unicast - PKI-encrypted to that node's public key, readable
// by it alone - or a directed broadcast: addressed to one node but encrypted with the channel PSK,
// so every channel member can still read it and only the addressee acts on it.
//
// Bits 0-3 say who may pull the packet with a want_response request; bits 4-7 say whether a packet
// with a destination goes as a unicast or a directed broadcast. The reply bits run in the module's
// allocReply(), before any throttle, so a refused request costs no airtime and no "no response" NAK
// (ignoreRequest is set). The crypto bits run in the Router (wouldEncryptWithPKC) for every from-us
// packet with a destination on the port: routine sends, replies to pollers, and phone-originated
// sends alike.
//
// Pitfalls:
//  - The policy is per port, not per payload type. Every telemetry sub-type (device, environment,
//    health, ...) shares TelemetryConfig.policy_flags because they all ride TELEMETRY_APP and the
//    Router never decodes payloads. Health cannot be locked down harder than battery level here.
//  - Do not read a port's flags from another port's config. Paxcounter and telemetry look alike
//    (both are metrics) but each has its own; a port that borrows another's is a coupling a client
//    cannot see in the config it is editing.
//  - REPLY_ONLY_TO_DEST compares against the port's routine destination. A port with no
//    destination (neighbour info) or an unset one admits nobody on the mesh, on purpose: the
//    operator asked for "only the collector" and there is no collector. Never widen it to "anyone".
//  - The phone reaches its own node as a request from our own node number (MeshModule answers
//    those deliberately). replyPolicyAllows() lets it through before any bit is read; a gate that
//    checks the bits first locks the phone out of its own device.
//  - Ignored nodes are refused before the bits are read, so "ignored" is never a policy choice a
//    flag can undo.
//  - PKC_ALWAYS (unicast only) to a destination whose key is not held fails at encode on every
//    interval; the admin setters refuse such a config via pkcAlwaysDestsHaveKeys(). Check the
//    config being *set*, with its own destinations, not the stored one.
//  - PKC_ALWAYS and PKC_NEVER together resolve to PKC_ALWAYS: a directed broadcast is readable by
//    the whole channel, so it is the one not to fall into by accident.
//  - The Router never unicasts POSITION_APP; a position with a destination is a directed broadcast
//    unless PKC_ALWAYS is set on the position port, and then the destination's own channel sets the
//    precision (PositionModule::directedSendChannel). Do not unicast position anywhere else.
//  - With neither crypto bit set, a destination whose key is not held gets a directed broadcast
//    rather than nothing. Every other port refuses such a send outright (PKI_SEND_FAIL_PUBLIC_KEY),
//    and PKC_ALWAYS restores that. The fallback is intended for these ports (a collector can be
//    behind a key-less node); it is not a pattern to copy to text messages or admin.

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
    if ((flags & meshtastic_PortPolicyFlags_REPLY_ONLY_TO_DEST) && (!dest || from != dest))
        return false;
    if ((flags & meshtastic_PortPolicyFlags_REPLY_TO_FAVOURITES_ONLY) && nodeDB && !nodeDB->isFavorite(from))
        return false;
    return true;
}

/// Admin gate: PKC_ALWAYS means unicast only, so every routine destination must have a public key
/// in NodeDB or the routine send fails at encode on every interval.
static inline bool pkcAlwaysDestsHaveKeys(uint32_t flags, const uint32_t *dests, size_t count)
{
    if (!(flags & meshtastic_PortPolicyFlags_PKC_ALWAYS))
        return true;
    for (size_t i = 0; i < count; i++) {
        meshtastic_NodeInfoLite_public_key_t key;
        if (dests[i] && !(nodeDB && nodeDB->copyPublicKey(dests[i], key))) {
            LOG_WARN("PKC_ALWAYS refused: no public key for destination 0x%08x", dests[i]);
            return false;
        }
    }
    return true;
}
