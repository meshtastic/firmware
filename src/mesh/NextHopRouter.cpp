#include "NextHopRouter.h"
#include "MeshTypes.h"
#include "meshUtils.h"
#if !MESHTASTIC_EXCLUDE_TRACEROUTE
#include "modules/TraceRouteModule.h"
#endif
#if HAS_TRAFFIC_MANAGEMENT
#include "modules/TrafficManagementModule.h"
#endif
#include "NodeDB.h"

NextHopRouter::NextHopRouter() {}

PendingPacket::PendingPacket(meshtastic_MeshPacket *p, uint8_t numRetransmissions)
{
    packet = p;
    this->numRetransmissions = numRetransmissions - 1; // We subtract one, because we assume the user just did the first send
}

/**
 * Send a packet
 */
ErrorCode NextHopRouter::send(meshtastic_MeshPacket *p)
{
    // Add any messages _we_ send to the seen message list (so we will ignore all retransmissions we see)
    p->relay_node = nodeDB->getLastByteOfNodeNum(getNodeNum()); // First set the relayer to us
    wasSeenRecently(p);                                         // FIXME, move this to a sniffSent method

    p->next_hop = getNextHop(p->to, p->relay_node).value_or(NO_NEXT_HOP_PREFERENCE); // set the next hop
    LOG_DEBUG("Setting next hop for packet with dest %x to %x", p->to, p->next_hop);

    // If it's from us, ReliableRouter already handles retransmissions if want_ack is set. If a next hop is set and hop limit is
    // not 0 or want_ack is set, start retransmissions
    if ((!isFromUs(p) || !p->want_ack) && p->next_hop != NO_NEXT_HOP_PREFERENCE && (p->hop_limit > 0 || p->want_ack))
        startRetransmission(packetPool.allocCopy(*p)); // start retransmission for relayed packet

    return Router::send(p);
}

bool NextHopRouter::shouldFilterReceived(const meshtastic_MeshPacket *p)
{
    bool wasFallback = false;
    bool weWereNextHop = false;
    bool wasUpgraded = false;
    bool seenRecently = wasSeenRecently(p, true, &wasFallback, &weWereNextHop,
                                        &wasUpgraded); // Updates history; returns false when an upgrade is detected

    // Handle hop_limit upgrade scenario for rebroadcasters
    if (wasUpgraded && perhapsHandleUpgradedPacket(p)) {
        return true; // we handled it, so stop processing
    }

    if (seenRecently) {
        printPacket("Ignore dupe incoming msg", p);

        if (p->transport_mechanism == meshtastic_MeshPacket_TransportMechanism_TRANSPORT_LORA) {
            rxDupe++;
            stopRetransmission(p->from, p->id);
        }

        // If it was a fallback to flooding, try to relay again
        if (wasFallback) {
            LOG_INFO("Fallback to flooding from relay_node=0x%x", p->relay_node);
            // Check if it's still in the Tx queue, if not, we have to relay it again
            if (!findInTxQueue(p->from, p->id)) {
                reprocessPacket(p);
                perhapsRebroadcast(p);
            }
        } else {
            bool isRepeated = getHopsAway(*p) == 0;
            // If repeated and not in Tx queue anymore, try relaying again, or if we are the destination, send the ACK again
            if (isRepeated) {
                if (!findInTxQueue(p->from, p->id)) {
                    reprocessPacket(p);
                    if (!perhapsRebroadcast(p) && isToUs(p) && p->want_ack) {
                        sendAckNak(meshtastic_Routing_Error_NONE, getFrom(p), p->id, p->channel, 0);
                    }
                }
            } else if (!weWereNextHop) {
                perhapsCancelDupe(p); // If it's a dupe, cancel relay if we were not explicitly asked to relay
            }
        }
        return true;
    }

    return Router::shouldFilterReceived(p);
}

void NextHopRouter::sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c)
{
    NodeNum ourNodeNum = getNodeNum();
    uint8_t ourRelayID = nodeDB->getLastByteOfNodeNum(ourNodeNum);
    bool isAckorReply = (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) &&
                        (p->decoded.request_id != 0 || p->decoded.reply_id != 0);
    if (isAckorReply) {
        // Update next-hop for the original transmitter of this successful transmission to the relay node, but ONLY if "from"
        // is not 0 (means implicit ACK) and original packet was also relayed by this node, or we sent it directly to the
        // destination
        if (p->from != 0) {
            meshtastic_NodeInfoLite *origTx = nodeDB->getMeshNode(p->from);
            // Either relayer of ACK was also a relayer of the packet, or we were the *only* relayer and the ACK came
            // directly from the destination. checkRelayers is read-only on PacketHistory and O(1), so we run it even
            // when origTx is absent — that lets us still capture the confirmed hop into the TMM overflow cache below.
            // Single lookup for both relayer checks on the same (request_id, to) pair
            bool wasAlreadyRelayer = false;
            bool weWereSoleRelayer = false;
            bool weWereRelayer = false;
            checkRelayers(p->relay_node, ourRelayID, p->decoded.request_id, p->to, &wasAlreadyRelayer, &weWereRelayer,
                          &weWereSoleRelayer);
            if ((weWereRelayer && wasAlreadyRelayer) || (getHopsAway(*p) == 0 && weWereSoleRelayer)) {
                // M1/M2: only learn a next hop whose last byte maps to a single plausible relay. On a dense
                // mesh the byte may be ambiguous; storing it would aim future DMs at the wrong node. This gate
                // now protects BOTH the hot-store route (NodeInfoLite.next_hop) AND the TMM overflow cache —
                // the overflow cache deliberately holds many more next-hop bytes (long-tail nodes), so it is
                // even more collision-prone and must never store an ambiguous byte either. Ambiguous/unknown
                // -> store nothing and keep flooding (safe).
                if (nodeDB->resolveUniqueLastByte(p->relay_node, /*requireDirectNeighbor=*/false)) {
                    if (origTx && origTx->next_hop != p->relay_node) { // Not already set
                        LOG_INFO("Update next hop of 0x%x to 0x%x based on ACK/reply (was relayer %d we were sole %d)", p->from,
                                 p->relay_node, wasAlreadyRelayer, weWereSoleRelayer);
                        origTx->next_hop = p->relay_node;
                    }
                    noteRouteLearned(p->from, p->relay_node, millis()); // M3: anchor freshness (hot or overflow route)
#if HAS_TRAFFIC_MANAGEMENT
                    // Mirror the confirmed (and now unique-resolved) hop into the TMM overflow cache so it
                    // survives even when the source isn't (or is no longer) in the hot NodeDB.
                    if (trafficManagementModule)
                        trafficManagementModule->setNextHop(p->from, p->relay_node);
#endif
                } else {
                    LOG_DEBUG("Not learning next hop for 0x%x: relay byte 0x%x ambiguous/unknown; keep flooding", p->from,
                              p->relay_node);
                }
            }
        }
        if (!isToUs(p)) {
            Router::cancelSending(p->to, p->decoded.request_id); // cancel rebroadcast for this DM
            // stop retransmission for the original packet
            stopRetransmission(p->to, p->decoded.request_id); // for original packet, from = to and id = request_id
        }
    }

    perhapsRebroadcast(p);

    // handle the packet as normal
    Router::sniffReceived(p, c);
}

/* Check if we should be rebroadcasting this packet if so, do so. */
bool NextHopRouter::perhapsRebroadcast(const meshtastic_MeshPacket *p)
{
    // Check if traffic management wants to exhaust this packet's hops
    bool exhaustHops = false;
#if HAS_TRAFFIC_MANAGEMENT
    if (trafficManagementModule && trafficManagementModule->shouldExhaustHops(*p)) {
        exhaustHops = true;
    }
#endif

    // Allow rebroadcast if hop_limit > 0 OR if we're exhausting hops (which sets hop_limit = 0 but still needs one relay)
    if (!isToUs(p) && !isFromUs(p) && (p->hop_limit > 0 || exhaustHops)) {
        if (p->id != 0) {
            if (isRebroadcaster()) {
                // NOTE: this is a self-identity match (is the addressed next_hop OUR last byte?), so it
                // cannot be hardened with resolveLastByte() — a remote node that legitimately shares our
                // last byte will also match here and rebroadcast. That residual collision needs a wider
                // on-wire field to fix. M1/M2 instead shrink the blast radius by reducing how often an
                // ambiguous next_hop byte is ever learned (sniffReceived) or originated (getNextHop).
                if (p->next_hop == NO_NEXT_HOP_PREFERENCE || p->next_hop == nodeDB->getLastByteOfNodeNum(getNodeNum())) {
                    meshtastic_MeshPacket *tosend = packetPool.allocCopy(*p); // keep a copy because we will be sending it
                    LOG_INFO("Rebroadcast received message coming from %x", p->relay_node);

                    // If exhausting hops, force hop_limit = 0 regardless of other logic
                    if (exhaustHops) {
                        tosend->hop_limit = 0;
                        LOG_INFO("Traffic management: exhausting hops for 0x%08x, setting hop_limit=0", getFrom(p));
                    } else if (shouldDecrementHopLimit(p)) {
                        // Use shared logic to determine if hop_limit should be decremented
                        tosend->hop_limit--; // bump down the hop count
                    } else {
                        LOG_INFO("favorite-ROUTER/CLIENT_BASE-to-ROUTER/CLIENT_BASE rebroadcast: preserving hop_limit");
                    }
#if USERPREFS_EVENT_MODE
                    if (tosend->hop_limit > 2) {
                        // if we are "correcting" the hop_limit, "correct" the hop_start by the same amount to preserve hops away.
                        tosend->hop_start -= (tosend->hop_limit - 2);
                        tosend->hop_limit = 2;
                    }
#endif

                    if (p->next_hop == NO_NEXT_HOP_PREFERENCE) {
                        FloodingRouter::send(tosend);
                    } else {
                        NextHopRouter::send(tosend);
                    }

                    return true;
                }
            } else {
                LOG_DEBUG("No rebroadcast: Role = CLIENT_MUTE or Rebroadcast Mode = NONE");
            }
        } else {
            LOG_DEBUG("Ignore 0 id broadcast");
        }
    }

    return false;
}

/**
 * Get the next hop for a destination, given the relay node
 * @return the node number of the next hop, 0 if no preference (fallback to FloodingRouter)
 */
std::optional<uint8_t> NextHopRouter::getNextHop(NodeNum to, uint8_t relay_node)
{
    if (isBroadcast(to))
        return std::nullopt;

    // Hot store first: a direct array hit on the live NodeDB entry.
    meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(to);
    if (node && node->next_hop) {
        // M3: proactively decay a stale or repeatedly-failing route back to flooding, so a dead hop
        // isn't trusted on the next DM's first (and on dense meshes, slowest) attempt. We only act on
        // a health record that still matches the stored byte; a next_hop set by another path (e.g.
        // TraceRouteModule) with no matching record is left authoritative.
        const RouteHealth *h = findRouteHealth(to);
        if (h && h->lastNextHop == node->next_hop && isRouteStale(*h, millis())) {
            LOG_INFO("Next hop 0x%x for 0x%x is stale (age/fails); flood and clear", node->next_hop, to);
            node->next_hop = NO_NEXT_HOP_PREFERENCE; // clear persisted route
            clearRouteHealth(to);                    // clear RAM health
            return std::nullopt;
        }

        // We are careful not to return the relay node as the next hop
        if (node->next_hop != relay_node) {
            // M1/M2: only emit a stored next_hop if its last byte still maps to a UNIQUE, currently
            // reachable direct neighbor. On a dense mesh the last byte collides, so an ambiguous byte
            // would unicast a hint toward the wrong physical node; if the neighbor has gone away we'd
            // unicast into a void. In both cases flood instead (managed flooding still delivers).
            ResolvedNode r = nodeDB->resolveLastByte(node->next_hop, /*requireDirectNeighbor=*/true);
            if (r.status == LastByteResolution::Unique)
                return node->next_hop;
            LOG_WARN("Next hop 0x%x for 0x%x %s; set no pref", node->next_hop, to,
                     r.status == LastByteResolution::Ambiguous ? "ambiguous among neighbors" : "not a known neighbor");
        } else
            LOG_WARN("Next hop for 0x%x is 0x%x, same as relayer; set no pref", to, node->next_hop);
    }

#if HAS_TRAFFIC_MANAGEMENT
    // Fallback: TMM overflow cache holds confirmed hops for nodes that have aged out of the hot store.
    // It is the same byte source/confidence as NodeInfoLite.next_hop, so it gets the same M1/M2/M3
    // protection: decay a stale/failing route, then only emit a byte that still resolves to a unique
    // reachable neighbor. Without this the overflow cache (which holds MORE bytes for MORE nodes) would
    // reintroduce exactly the silent-misroute that M1/M2 closes on the hot path.
    if (trafficManagementModule) {
        uint8_t hint = trafficManagementModule->getNextHopHint(to);
        if (hint && hint != relay_node) {
            const RouteHealth *h = findRouteHealth(to);
            if (h && h->lastNextHop == hint && isRouteStale(*h, millis())) {
                LOG_INFO("TMM next hop 0x%x for 0x%x is stale (age/fails); flood and clear", hint, to);
                trafficManagementModule->clearNextHop(to); // clear overflow route (setNextHop won't store 0)
                clearRouteHealth(to);                      // clear RAM health
                return std::nullopt;
            }
            ResolvedNode r = nodeDB->resolveLastByte(hint, /*requireDirectNeighbor=*/true);
            if (r.status == LastByteResolution::Unique) {
                LOG_DEBUG("Next hop for 0x%x is 0x%x (TMM cache)", to, hint);
                return hint;
            }
            LOG_WARN("TMM next hop 0x%x for 0x%x %s; set no pref", hint, to,
                     r.status == LastByteResolution::Ambiguous ? "ambiguous among neighbors" : "not a known neighbor");
        }
    }
#endif

    return std::nullopt;
}

PendingPacket *NextHopRouter::findPendingPacket(GlobalPacketId key)
{
    auto old = pending.find(key); // If we have an old record, someone messed up because id got reused
    if (old != pending.end()) {
        return &old->second;
    } else
        return NULL;
}

/**
 * Stop any retransmissions we are doing of the specified node/packet ID pair
 */
bool NextHopRouter::stopRetransmission(NodeNum from, PacketId id)
{
    auto key = GlobalPacketId(from, id);
    return stopRetransmission(key);
}

bool NextHopRouter::roleAllowsCancelingFromTxQueue(const meshtastic_MeshPacket *p)
{
    // Return true if we're allowed to cancel a packet in the txQueue (so we may never transmit it even once)

    // Return false for roles like ROUTER, ROUTER_LATE which should always transmit the packet at least once.

    return roleAllowsCancelingDupe(p); // same logic as FloodingRouter::roleAllowsCancelingDupe
}

bool NextHopRouter::stopRetransmission(GlobalPacketId key)
{
    auto old = findPendingPacket(key);
    if (old) {
        auto p = old->packet;
        /* Only when we already transmitted a packet via LoRa, we will cancel the packet in the Tx queue
          to avoid canceling a transmission if it was ACKed super fast via MQTT */
        if (old->numRetransmissions < NUM_RELIABLE_RETX - 1) {
            // We only cancel it if we are the original sender or if we're not a router(_late)
            if (isFromUs(p) || roleAllowsCancelingFromTxQueue(p)) {
                // remove the 'original' (identified by originator and packet->id) from the txqueue and free it
                cancelSending(getFrom(p), p->id);
            }
        }

        // Regardless of whether or not we canceled this packet from the txQueue, remove it from our pending list so it
        // doesn't get scheduled again. (This is the core of stopRetransmission.)
        auto numErased = pending.erase(key);
        assert(numErased == 1);

        // When we remove an entry from pending, always be sure to release the copy of the packet that was allocated in the
        // call to startRetransmission.
        packetPool.release(p);

        return true;
    } else
        return false;
}

/**
 * Add p to the list of packets to retransmit occasionally.  We will free it once we stop retransmitting.
 */
PendingPacket *NextHopRouter::startRetransmission(meshtastic_MeshPacket *p, uint8_t numReTx)
{
    auto id = GlobalPacketId(p);
    auto rec = PendingPacket(p, numReTx);

    stopRetransmission(getFrom(p), p->id);

    setNextTx(&rec);
    pending[id] = rec;

    return &pending[id];
}

/**
 * Do any retransmissions that are scheduled (FIXME - for the time being called from loop)
 */
int32_t NextHopRouter::doRetransmissions()
{
    uint32_t now = millis();
    int32_t d = INT32_MAX;

    // FIXME, we should use a better datastructure rather than walking through this map.
    // for(auto el: pending) {
    for (auto it = pending.begin(), nextIt = it; it != pending.end(); it = nextIt) {
        ++nextIt; // we use this odd pattern because we might be deleting it...
        auto &p = it->second;

        bool stillValid = true; // assume we'll keep this record around

        // FIXME, handle 51 day rolloever here!!!
        if (p.nextTxMsec <= now) {
            if (p.numRetransmissions == 0) {
                if (isFromUs(p.packet)) {
                    LOG_DEBUG("Reliable send failed, returning a nak for fr=0x%x,to=0x%x,id=0x%x", p.packet->from, p.packet->to,
                              p.packet->id);
                    sendAckNak(meshtastic_Routing_Error_MAX_RETRANSMIT, getFrom(p.packet), p.packet->id, p.packet->channel);
                }
                // Note: we don't stop retransmission here, instead the Nak packet gets processed in sniffReceived
                stopRetransmission(it->first);
                stillValid = false; // just deleted it
            } else {
                LOG_DEBUG("Sending retransmission fr=0x%x,to=0x%x,id=0x%x, tries left=%d", p.packet->from, p.packet->to,
                          p.packet->id, p.numRetransmissions);

                if (!isBroadcast(p.packet->to)) {
                    if (p.numRetransmissions == 1) {
                        // Last retransmission: this directed delivery went un-ACKed. Record the failure
                        // (M3 — accumulates across DMs to age out a flapping/dead route) and reset
                        // next_hop so the final try falls back to FloodingRouter.
                        noteRouteFailure(p.packet->to);
                        p.packet->next_hop = NO_NEXT_HOP_PREFERENCE;
                        // Also reset it in the nodeDB
                        meshtastic_NodeInfoLite *sentTo = nodeDB->getMeshNode(p.packet->to);
                        if (sentTo) {
                            LOG_INFO("Resetting next hop for packet with dest 0x%x\n", p.packet->to);
                            sentTo->next_hop = NO_NEXT_HOP_PREFERENCE;
                        }
#if HAS_TRAFFIC_MANAGEMENT
                        if (trafficManagementModule) {
                            trafficManagementModule->clearNextHop(p.packet->to);
                        }
#endif
                        FloodingRouter::send(packetPool.allocCopy(*p.packet));
                    } else {
#if NEXTHOP_EARLY_FLOOD_ON_UNVERIFIED
                        // M4 (gated): if the route isn't proven healthy, don't spend a second directed
                        // attempt — start flooding one retry sooner to cut recovery latency. A verified
                        // route (fresh, zero recent failures) keeps the unchanged directed-retry path so
                        // the sparse-mesh happy path is untouched.
                        RouteHealth *h = findRouteHealth(p.packet->to);
                        bool verified = h && h->consecutiveFailures == 0 && !isRouteStale(*h, now);
                        if (!verified) {
                            p.packet->next_hop = NO_NEXT_HOP_PREFERENCE;
                            meshtastic_NodeInfoLite *sentTo = nodeDB->getMeshNode(p.packet->to);
                            if (sentTo)
                                sentTo->next_hop = NO_NEXT_HOP_PREFERENCE;
                            FloodingRouter::send(packetPool.allocCopy(*p.packet));
                        } else {
                            NextHopRouter::send(packetPool.allocCopy(*p.packet));
                        }
#else
                        NextHopRouter::send(packetPool.allocCopy(*p.packet));
#endif
                    }
                } else {
                    // Note: we call the superclass version because we don't want to have our version of send() add a new
                    // retransmission record
                    FloodingRouter::send(packetPool.allocCopy(*p.packet));
                }

                // Queue again
                --p.numRetransmissions;
                setNextTx(&p);
            }
        }

        if (stillValid) {
            // Update our desired sleep delay
            int32_t t = p.nextTxMsec - now;

            d = min(t, d);
        }
    }

    return d;
}

void NextHopRouter::setNextTx(PendingPacket *pending)
{
    assert(iface);
    auto d = iface->getRetransmissionMsec(pending->packet);
    pending->nextTxMsec = millis() + d;
    LOG_DEBUG("Setting next retransmission in %u msecs: ", d);
    printPacket("", pending->packet);
    setReceivedMessage(); // Run ASAP, so we can figure out our correct sleep time
}

// ---------------------------------------------------------------------------
// M3: RAM route-health table. Bounded array with reuse-oldest eviction (same discipline as
// PacketHistory). All age comparisons use unsigned subtraction so they survive the 49.7-day millis()
// rollover. dest == 0 marks an empty slot; learnedAtMsec is normalized to 1 on write so an occupied
// slot is never read as infinitely old.
// ---------------------------------------------------------------------------

RouteHealth *NextHopRouter::findRouteHealth(NodeNum dest)
{
    if (dest == 0)
        return nullptr;
    for (auto &h : routeHealth)
        if (h.dest == dest)
            return &h;
    return nullptr;
}

RouteHealth *NextHopRouter::getOrAllocRouteHealth(NodeNum dest, uint32_t now)
{
    if (dest == 0)
        return nullptr;

    RouteHealth *oldest = &routeHealth[0];
    RouteHealth *freeSlot = nullptr;
    for (auto &h : routeHealth) {
        if (h.dest == dest)
            return &h; // existing record
        if (h.dest == 0) {
            if (!freeSlot)
                freeSlot = &h; // remember the first free slot; prefer it over evicting
            continue;
        }
        // Track the oldest occupied slot in case the table is full (rollover-safe).
        if ((uint32_t)(now - h.learnedAtMsec) > (uint32_t)(now - oldest->learnedAtMsec))
            oldest = &h;
    }
    // Claim the free slot if there is one, else reuse the oldest. Reset before use and stamp the dest
    // so the record is findable.
    RouteHealth *slot = freeSlot ? freeSlot : oldest;
    *slot = RouteHealth{};
    slot->dest = dest;
    return slot;
}

void NextHopRouter::noteRouteLearned(NodeNum dest, uint8_t nextHop, uint32_t now)
{
    if (dest == 0 || nextHop == NO_NEXT_HOP_PREFERENCE)
        return;
    RouteHealth *h = getOrAllocRouteHealth(dest, now);
    if (!h)
        return;
    // A genuinely new next hop earns a clean slate; re-learning the SAME hop keeps the accumulated
    // failure count so an asymmetric reverse path that keeps re-teaching a dead forward hop still ages
    // out instead of resetting the counter every time.
    if (h->lastNextHop != nextHop) {
        h->lastNextHop = nextHop;
        h->consecutiveFailures = 0;
    }
    h->learnedAtMsec = now ? now : 1;
}

void NextHopRouter::noteRouteSuccess(NodeNum dest, uint32_t now)
{
    RouteHealth *h = findRouteHealth(dest);
    if (!h)
        return; // only routes we actually learned have health to refresh
    h->consecutiveFailures = 0;
    h->learnedAtMsec = now ? now : 1;
}

void NextHopRouter::noteRouteFailure(NodeNum dest)
{
    RouteHealth *h = findRouteHealth(dest);
    if (!h)
        return; // nothing to penalize (we were flooding, or never learned a route here)
    if (h->consecutiveFailures < 255)
        h->consecutiveFailures++;
}

bool NextHopRouter::isRouteStale(const RouteHealth &h, uint32_t now) const
{
    if (h.consecutiveFailures >= ROUTE_FAILURE_THRESHOLD)
        return true;
    return (uint32_t)(now - h.learnedAtMsec) >= ROUTE_TTL_MSEC;
}

void NextHopRouter::clearRouteHealth(NodeNum dest)
{
    RouteHealth *h = findRouteHealth(dest);
    if (h)
        *h = RouteHealth{};
}
