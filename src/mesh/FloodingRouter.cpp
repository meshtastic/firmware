#include "FloodingRouter.h"

#include "configuration.h"
#include "mesh-pb-constants.h"
#include <RTC.h>

FloodingRouter::FloodingRouter() {}

/**
 * Send a packet on a suitable interface.  This routine will
 * later free() the packet to pool.  This routine is not allowed to stall.
 * If the txmit queue is full it might return an error
 */
ErrorCode FloodingRouter::send(meshtastic_MeshPacket *p)
{
    // Add any messages _we_ send to the seen message list (so we will ignore all retransmissions we see)
    wasSeenRecently(p); // FIXME, move this to a sniffSent method

    CoverageFilter coverage;
    // Is there anything upstream of this?
    // I think not, but if so, we need to merge coverage.
    // loadCoverageFilterFromPacket(p, coverage);

    // Add our coverage (neighbors, etc.) so they are in the filter from the get-go
    mergeMyCoverage(coverage);

    // Save the coverage bits into the packet:
    storeCoverageFilterInPacket(coverage, p);

    return Router::send(p);
}

bool FloodingRouter::shouldFilterReceived(const meshtastic_MeshPacket *p)
{
    if (wasSeenRecently(p)) { // Note: this will also add a recent packet record
        printPacket("Ignore dupe incoming msg", p);
        rxDupe++;
        if (config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER &&
            config.device.role != meshtastic_Config_DeviceConfig_Role_REPEATER &&
            config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER_LATE) {
            // cancel rebroadcast of this message *if* there was already one, unless we're a router/repeater!
            if (Router::cancelSending(p->from, p->id))
                txRelayCanceled++;
        }
        if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER_LATE && iface) {
            iface->clampToLateRebroadcastWindow(getFrom(p), p->id);
        }

        /* If the original transmitter is doing retransmissions (hopStart equals hopLimit) for a reliable transmission, e.g., when
        the ACK got lost, we will handle the packet again to make sure it gets an ACK to its packet. */
        bool isRepeated = p->hop_start > 0 && p->hop_start == p->hop_limit;
        if (isRepeated) {
            LOG_DEBUG("Repeated reliable tx");
            if (!perhapsRebroadcast(p) && isToUs(p) && p->want_ack) {
                // FIXME - channel index should be used, but the packet is still encrypted here
                sendAckNak(meshtastic_Routing_Error_NONE, getFrom(p), p->id, 0, 0);
            }
        }

        return true;
    }

    return Router::shouldFilterReceived(p);
}

bool FloodingRouter::isRebroadcaster()
{
    return config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE &&
           config.device.rebroadcast_mode != meshtastic_Config_DeviceConfig_RebroadcastMode_NONE;
}

bool FloodingRouter::perhapsRebroadcast(const meshtastic_MeshPacket *p)
{
    if (!isToUs(p) && (p->hop_limit > 0) && !isFromUs(p)) {
        if (p->id != 0) {
            if (isRebroadcaster()) {
                CoverageFilter incomingCoverage;
                loadCoverageFilterFromPacket(p, incomingCoverage);

                float forwardProb = calculateForwardProbability(incomingCoverage, p->from, p->relay_node);

                float rnd = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
                if (rnd <= forwardProb) {

                    meshtastic_MeshPacket *tosend = packetPool.allocCopy(*p); // keep a copy because we will be sending it

                    tosend->hop_limit--; // bump down the hop count
#if USERPREFS_EVENT_MODE
                    if (tosend->hop_limit > 2) {
                        // if we are "correcting" the hop_limit, "correct" the hop_start by the same amount to preserve hops away.
                        tosend->hop_start -= (tosend->hop_limit - 2);
                        tosend->hop_limit = 2;
                    }
#endif

                    CoverageFilter updatedCoverage = incomingCoverage;
                    mergeMyCoverage(updatedCoverage);

                    storeCoverageFilterInPacket(updatedCoverage, tosend);

                    LOG_INFO("Rebroadcasting packet ID=0x%x with ForwardProb=%.2f", p->id, forwardProb);

                    // Note: we are careful to resend using the original senders node id
                    // We are careful not to call our hooked version of send() - because we don't want to check this again
                    Router::send(tosend);

                    return true;
                } else {
                    LOG_INFO("No rebroadcast: Random number %f > Forward Probability %f", rnd, forwardProb);
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

void FloodingRouter::sniffReceived(const meshtastic_MeshPacket *p, const meshtastic_Routing *c)
{
    bool isAckorReply = (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) && (p->decoded.request_id != 0);
    if (isAckorReply && !isToUs(p) && !isBroadcast(p->to)) {
        // do not flood direct message that is ACKed or replied to
        LOG_DEBUG("Rxd an ACK/reply not for me, cancel rebroadcast");
        Router::cancelSending(p->to, p->decoded.request_id); // cancel rebroadcast for this DM
    }

    perhapsRebroadcast(p);

    // handle the packet as normal
    Router::sniffReceived(p, c);
}

void FloodingRouter::loadCoverageFilterFromPacket(const meshtastic_MeshPacket *p, CoverageFilter &filter)
{
    // If packet has coverage bytes (16 bytes), copy them into filter
    // e.g. p->coverage_filter is a 16-byte array in your packet struct
    std::array<uint8_t, BLOOM_FILTER_SIZE_BYTES> bits;
    memcpy(bits.data(), p->coverage_filter.bytes, BLOOM_FILTER_SIZE_BYTES);
    filter.setBits(bits);
}

void FloodingRouter::storeCoverageFilterInPacket(const CoverageFilter &filter, meshtastic_MeshPacket *p)
{
    auto bits = filter.getBits();
    p->coverage_filter.size = BLOOM_FILTER_SIZE_BYTES;
    memcpy(p->coverage_filter.bytes, bits.data(), BLOOM_FILTER_SIZE_BYTES);
}

void FloodingRouter::mergeMyCoverage(CoverageFilter &coverage)
{
    // Retrieve recent direct neighbors within the time window
    std::vector<meshtastic_RelayNode> recentNeighbors = nodeDB->getCoveredNodes();
    for (auto &relay : recentNeighbors) {
        coverage.add(relay.num);
    }

    // Always add ourselves to prevent a rebroadcast for a packet we've already seen
    coverage.add(nodeDB->getNodeNum());
}

float FloodingRouter::calculateForwardProbability(const CoverageFilter &incoming, NodeNum from, NodeNum relayNode)
{
#ifndef USERPREFS_USE_COVERAGE_FILTER
    LOG_DEBUG("Coverage filter is NOT enabled.");
    return 1.0f;
#endif
    // If we are a router or repeater, always forward because it's assumed these are in the most advantageous locations
    // Small meshes don't use coverage filter
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER ||
        config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER) {
        return 1.0f;
    }

    // Retrieve recent direct neighbors within the time window
    std::vector<meshtastic_RelayNode> recentNeighbors = nodeDB->getCoveredNodes();

    if (recentNeighbors.empty()) {
        // Having no direct neighbors is a sign that our coverage is
        // inconclusive, so we should forward the packet using UNKNOWN_COVERAGE_FORWARD_PROB
        // And if we truly have no neighbors, there is no harm in emitting another packet
        LOG_DEBUG("No recent direct neighbors to add coverage for.");
        return UNKNOWN_COVERAGE_FORWARD_PROB;
    }

    uint32_t now = getTime();
    // Count how many neighbors are NOT yet in the coverage
    float totalWeight = 0.0f;
    float uncoveredWeight = 0.0f;
    uint8_t neighbors = 0;
    uint8_t uncovered = 0;

    for (auto relay : recentNeighbors) {
        if (relay.num == from || relay.num == relayNode)
            continue;

        uint32_t age = now - relay.last_heard;
        float recency = computeRecencyWeight(age, RECENCY_THRESHOLD_MINUTES * 60);

        totalWeight += recency;
        neighbors += 1;
        if (!incoming.check(relay.num)) {
            uncoveredWeight += recency;
            uncovered += 1;
        }
    }

    float coverageRatio = 0.0f;

    // coverage only exists if neighbors are more than 0
    if (totalWeight > 0) {
        coverageRatio = uncoveredWeight / totalWeight;
    }

    float smallMeshCorrection = 0.0f;
    if (nodeDB->getNumOnlineMeshNodes(true) <= 10) {
        smallMeshCorrection = 0.5f;
    }
    float forwardProb = (coverageRatio * COVERAGE_SCALE_FACTOR) + smallMeshCorrection;

    // Clamp probability between BASE_FORWARD_PROB and 1
    forwardProb = std::min(std::max(forwardProb, BASE_FORWARD_PROB), 1.0f);

    LOG_DEBUG("CoverageRatio=%.2f, ForwardProb=%.2f (Uncovered=%d, Total=%zu)", coverageRatio, forwardProb, uncovered, neighbors);

    return forwardProb;
}

float FloodingRouter::computeRecencyWeight(uint32_t age, uint32_t timeWindowSecs)
{
    // A node just heard from age=0 => weight=1.0
    // A node at the edge of timeWindowSecs => weight approaches 0.
    // We clamp to [0, 1] just in case of rounding.
    float ratio = 1.0f - (static_cast<float>(age) / static_cast<float>(timeWindowSecs));
    return std::max(std::min(ratio, 1.0f), 0.0f);
}