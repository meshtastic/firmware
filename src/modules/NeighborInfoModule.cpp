#include "NeighborInfoModule.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include <Throttle.h>

NeighborInfoModule *neighborInfoModule;

/*
Prints a single neighbor info packet and associated neighbors
Uses LOG_DEBUG, which equates to Console.log
NOTE: For debugging only
*/
void NeighborInfoModule::printNeighborInfo(const char *header, const meshtastic_NeighborInfo *np)
{
    LOG_DEBUG("%s NEIGHBORINFO PACKET from Node 0x%x to Node 0x%x (last sent by 0x%x)\n", header, np->node_id,
              nodeDB->getNodeNum(), np->last_sent_by_id);
    LOG_DEBUG("Packet contains %d neighbors\n", np->neighbors_count);
    for (int i = 0; i < np->neighbors_count; i++) {
        LOG_DEBUG("Neighbor %d: node_id=0x%x, snr=%.2f\n", i, np->neighbors[i].node_id, np->neighbors[i].snr);
    }
}

/*
Prints the nodeDB neighbors
NOTE: for debugging only
*/
void NeighborInfoModule::printNodeDBNeighbors()
{
    LOG_DEBUG("Our NodeDB contains %d neighbors\n", neighbors.size());
    for (size_t i = 0; i < neighbors.size(); i++) {
        LOG_DEBUG("Node %d: node_id=0x%x, snr=%.2f\n", i, neighbors[i].node_id, neighbors[i].snr);
    }
}

/* Send our initial owner announcement 35 seconds after we start (to give network time to setup) */
NeighborInfoModule::NeighborInfoModule()
    : ProtobufModule("neighborinfo", meshtastic_PortNum_NEIGHBORINFO_APP, &meshtastic_NeighborInfo_msg),
      concurrency::OSThread("NeighborInfoModule")
{
    ourPortNum = meshtastic_PortNum_NEIGHBORINFO_APP;
    nodeStatusObserver.observe(&nodeStatus->onNewStatus);

    if (moduleConfig.neighbor_info.enabled) {
        isPromiscuous = true; // Update neighbors from all packets
        setIntervalFromNow(Default::getConfiguredOrDefaultMs(moduleConfig.neighbor_info.update_interval,
                                                             default_telemetry_broadcast_interval_secs));
    } else {
        LOG_DEBUG("NeighborInfoModule is disabled\n");
        disable();
    }
}

/*
Collect neighbor info from the nodeDB's history, capping at a maximum number of entries and max time
Assumes that the neighborInfo packet has been allocated
@returns the number of entries collected
*/
uint32_t NeighborInfoModule::collectNeighborInfo(meshtastic_NeighborInfo *neighborInfo)
{
    NodeNum my_node_id = nodeDB->getNodeNum();
    neighborInfo->node_id = my_node_id;
    neighborInfo->last_sent_by_id = my_node_id;
    neighborInfo->node_broadcast_interval_secs = moduleConfig.neighbor_info.update_interval;

    cleanUpNeighbors();

    for (auto nbr : neighbors) {
        if ((neighborInfo->neighbors_count < MAX_NUM_NEIGHBORS) && (nbr.node_id != my_node_id)) {
            neighborInfo->neighbors[neighborInfo->neighbors_count].node_id = nbr.node_id;
            neighborInfo->neighbors[neighborInfo->neighbors_count].snr = nbr.snr;
            // Note: we don't set the last_rx_time and node_broadcast_intervals_secs here, because we don't want to send this over
            // the mesh
            neighborInfo->neighbors_count++;
        }
    }
    printNodeDBNeighbors();
    return neighborInfo->neighbors_count;
}

/*
  Remove neighbors from the database that we haven't heard from in a while
*/
void NeighborInfoModule::cleanUpNeighbors()
{
    NodeNum my_node_id = nodeDB->getNodeNum();
    for (auto it = neighbors.rbegin(); it != neighbors.rend();) {
        // We will remove a neighbor if we haven't heard from them in twice the broadcast interval
        if (!Throttle::isWithinTimespanMs(it->last_rx_time, it->node_broadcast_interval_secs * 2) &&
            (it->node_id != my_node_id)) {
            LOG_DEBUG("Removing neighbor with node ID 0x%x\n", it->node_id);
            it = std::vector<meshtastic_Neighbor>::reverse_iterator(
                neighbors.erase(std::next(it).base())); // Erase the element and update the iterator
        } else {
            ++it;
        }
    }
}

/* Send neighbor info to the mesh */
void NeighborInfoModule::sendNeighborInfo(NodeNum dest, bool wantReplies)
{
    meshtastic_NeighborInfo neighborInfo = meshtastic_NeighborInfo_init_zero;
    collectNeighborInfo(&neighborInfo);
    meshtastic_MeshPacket *p = allocDataProtobuf(neighborInfo);
    // send regardless of whether or not we have neighbors in our DB,
    // because we want to get neighbors for the next cycle
    p->to = dest;
    p->decoded.want_response = wantReplies;
    p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    printNeighborInfo("SENDING", &neighborInfo);
    service->sendToMesh(p, RX_SRC_LOCAL, true);
}

/*
Encompasses the full construction and sending packet to mesh
Will be used for broadcast.
*/
int32_t NeighborInfoModule::runOnce()
{
    if (airTime->isTxAllowedChannelUtil(true) && airTime->isTxAllowedAirUtil()) {
        sendNeighborInfo(NODENUM_BROADCAST, false);
    }
    return Default::getConfiguredOrDefaultMs(moduleConfig.neighbor_info.update_interval, default_neighbor_info_broadcast_secs);
}

/*
Collect a recieved neighbor info packet from another node
Pass it to an upper client; do not persist this data on the mesh
*/
bool NeighborInfoModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_NeighborInfo *np)
{
    if (np) {
        printNeighborInfo("RECEIVED", np);
        updateNeighbors(mp, np);
    } else if (mp.hop_start != 0 && mp.hop_start == mp.hop_limit) {
        // If the hopLimit is the same as hopStart, then it is a neighbor
        getOrCreateNeighbor(mp.from, mp.from, 0, mp.rx_snr); // Set the broadcast interval to 0, as we don't know it
    }
    // Allow others to handle this packet
    return false;
}

/*
Copy the content of a current NeighborInfo packet into a new one and update the last_sent_by_id to our NodeNum
*/
void NeighborInfoModule::alterReceivedProtobuf(meshtastic_MeshPacket &p, meshtastic_NeighborInfo *n)
{
    n->last_sent_by_id = nodeDB->getNodeNum();

    // Set updated last_sent_by_id to the payload of the to be flooded packet
    p.decoded.payload.size =
        pb_encode_to_bytes(p.decoded.payload.bytes, sizeof(p.decoded.payload.bytes), &meshtastic_NeighborInfo_msg, n);
}

void NeighborInfoModule::resetNeighbors()
{
    neighbors.clear();
}

void NeighborInfoModule::updateNeighbors(const meshtastic_MeshPacket &mp, const meshtastic_NeighborInfo *np)
{
    // The last sent ID will be 0 if the packet is from the phone, which we don't count as
    // an edge. So we assume that if it's zero, then this packet is from our node.
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag && mp.from) {
        getOrCreateNeighbor(mp.from, np->last_sent_by_id, np->node_broadcast_interval_secs, mp.rx_snr);
    }
}

meshtastic_Neighbor *NeighborInfoModule::getOrCreateNeighbor(NodeNum originalSender, NodeNum n,
                                                             uint32_t node_broadcast_interval_secs, float snr)
{
    // our node and the phone are the same node (not neighbors)
    if (n == 0) {
        n = nodeDB->getNodeNum();
    }
    // look for one in the existing list
    for (size_t i = 0; i < neighbors.size(); i++) {
        if (neighbors[i].node_id == n) {
            // if found, update it
            neighbors[i].snr = snr;
            neighbors[i].last_rx_time = getTime();
            // Only if this is the original sender, the broadcast interval corresponds to it
            if (originalSender == n && node_broadcast_interval_secs != 0)
                neighbors[i].node_broadcast_interval_secs = node_broadcast_interval_secs;
            return &neighbors[i];
        }
    }
    // otherwise, allocate one and assign data to it

    meshtastic_Neighbor new_nbr = meshtastic_Neighbor_init_zero;
    new_nbr.node_id = n;
    new_nbr.snr = snr;
    new_nbr.last_rx_time = getTime();
    // Only if this is the original sender, the broadcast interval corresponds to it
    if (originalSender == n && node_broadcast_interval_secs != 0)
        new_nbr.node_broadcast_interval_secs = node_broadcast_interval_secs;
    else // Assume the same broadcast interval as us for the neighbor if we don't know it
        new_nbr.node_broadcast_interval_secs = moduleConfig.neighbor_info.update_interval;

    if (neighbors.size() < MAX_NUM_NEIGHBORS) {
        neighbors.push_back(new_nbr);
    } else {
        // If we have too many neighbors, replace the oldest one
        LOG_WARN("Neighbor DB is full, replacing oldest neighbor\n");
        neighbors.erase(neighbors.begin());
        neighbors.push_back(new_nbr);
    }
    return &neighbors.back();
}