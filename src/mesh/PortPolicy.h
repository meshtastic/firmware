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
// Out of line in PortPolicy.cpp: one copy each, not one per including TU.
bool portPolicyFlags(meshtastic_PortNum port, uint32_t &flags);

/// Can this node use PKI at all? PKC_ALWAYS cannot be honoured without it, whatever key we hold for
/// the far end: the encoder needs our own private key, and licensed mode does not encrypt.
bool localPkiUsable();

/// Is `to` a routine destination configured for `port`? Telemetry's sub-types share one port, so any
/// of the five counts. Used to tell our own routine traffic from a unicast the client composed.
bool isRoutineDest(meshtastic_PortNum port, NodeNum to);

/// May we answer a want_response request from `from` under `flags`? Ignored nodes are refused
/// regardless; every bit only restricts. `dest` is the port's routine destination, 0 when none.
bool replyPolicyAllows(uint32_t flags, NodeNum from, NodeNum dest);

/// Admin gate: PKC_ALWAYS means unicast only, so every routine destination must have a public key
/// in NodeDB or the routine send fails at encode on every interval. `why`, when given, receives the
/// refusal text for the caller to put in front of the user; the header stays clear of MeshService.
bool pkcAlwaysDestsHaveKeys(uint32_t flags, const uint32_t *dests, size_t count, char *why = nullptr, size_t whyLen = 0);
