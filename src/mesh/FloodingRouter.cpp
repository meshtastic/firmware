#include "FloodingRouter.h"

#include "configuration.h"
#include "mesh-pb-constants.h"

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

    return Router::send(p);
}

bool FloodingRouter::shouldFilterReceived(const meshtastic_MeshPacket *p)
{
    if (wasSeenRecently(p)) { // Note: this will also add a recent packet record
        printPacket("Ignore dupe incoming msg", p);
        rxDupe++;
        if (config.device.role != meshtastic_Config_DeviceConfig_Role_ROUTER &&
            config.device.role != meshtastic_Config_DeviceConfig_Role_REPEATER) {
            // cancel rebroadcast of this message *if* there was already one, unless we're a router/repeater!
            if (Router::cancelSending(p->from, p->id))
                txRelayCanceled++;
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
                float forwardProb = calculateForwardProbability(p);
                float rnd = (float)rand() / (float)RAND_MAX;
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

                    LOG_INFO("Rebroadcast received floodmsg");
                    // Note: we are careful to resend using the original senders node id
                    // We are careful not to call our hooked version of send() - because we don't want to check this again
                    Router::send(tosend);

                    return true;
                } else {
                    LOG_DEBUG("No rebroadcast: Random number %f > Forward Probability %f", rnd, forwardProb);
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

float FloodingRouter::calculateForwardProbability(const meshtastic_MeshPacket *p)
{
    // Routers and repeaters always forward, so skip expensive calcs and return the highest value
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER ||
        config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER) {
        return 1.0f;
    }

    uint8_t RECENCY_THRESHOLD_MINUTES = 5;
    size_t neighborCount = nodeDB->getDistinctRecentDirectNeighborCount((RECENCY_THRESHOLD_MINUTES * 60));

    float NEIGHBOR_INFLUENCE_FACTOR = 0.1f;
    float neighborFactor = 1.0f / (1.0f + neighborCount * NEIGHBOR_INFLUENCE_FACTOR);

    float REDUNDANCY_INFLUENCE_FACTOR = 0.05f;
    int distinctSources = getDistinctSourcesCount(p->id);
    float redundancyFactor = 1.0f / (1.0f + distinctSources * REDUNDANCY_INFLUENCE_FACTOR);

    float LOAD_INFLUENCE_FACTOR = 0.01f;
    float LOAD_THRESHOLD_MINUTES = 2;
    float recentPacketRate = getRecentUniquePacketRate(LOAD_THRESHOLD_MINUTES * 60 * 1000);
    float loadFactor = 1.0f / (1.0f + recentPacketRate * LOAD_INFLUENCE_FACTOR);

    // Start from a base probability
    float BASELINE_FORWARDING_PROBABILITY = 1.0f;
    float prob = BASELINE_FORWARDING_PROBABILITY;

    // Adjust with observed factors
    prob *= neighborFactor;
    prob *= redundancyFactor;
    prob *= loadFactor;

    // Clamp probability
    prob = min(max(prob, 0.0f), 1.0f);
    return prob;
}