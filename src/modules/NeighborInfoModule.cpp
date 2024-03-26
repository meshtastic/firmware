#include "NeighborInfoModule.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"

#define MAX_NUM_NEIGHBORS 10 // also defined in NeighborInfo protobuf options
NeighborInfoModule *neighborInfoModule;

static const char *neighborInfoConfigFile = "/prefs/neighbors.proto";

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
    int num_neighbors = getNumNeighbors();
    LOG_DEBUG("Our NodeDB contains %d neighbors\n", num_neighbors);
    for (int i = 0; i < num_neighbors; i++) {
        const meshtastic_Neighbor *dbEntry = getNeighborByIndex(i);
        LOG_DEBUG("     Node %d: node_id=0x%x, snr=%.2f\n", i, dbEntry->node_id, dbEntry->snr);
    }
}

/* Send our initial owner announcement 35 seconds after we start (to give network time to setup) */
NeighborInfoModule::NeighborInfoModule()
    : ProtobufModule("neighborinfo", meshtastic_PortNum_NEIGHBORINFO_APP, &meshtastic_NeighborInfo_msg),
      concurrency::OSThread("NeighborInfoModule"), neighbors(neighborState.neighbors),
      numNeighbors(&neighborState.neighbors_count)
{
    ourPortNum = meshtastic_PortNum_NEIGHBORINFO_APP;

    if (moduleConfig.neighbor_info.enabled) {
        isPromiscuous = true; // Update neighbors from all packets
        this->loadProtoForModule();
        setIntervalFromNow(35 * 1000);
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
    uint my_node_id = nodeDB->getNodeNum();
    neighborInfo->node_id = my_node_id;
    neighborInfo->last_sent_by_id = my_node_id;
    neighborInfo->node_broadcast_interval_secs = moduleConfig.neighbor_info.update_interval;

    int num_neighbors = cleanUpNeighbors();

    for (int i = 0; i < num_neighbors; i++) {
        const meshtastic_Neighbor *dbEntry = getNeighborByIndex(i);
        if ((neighborInfo->neighbors_count < MAX_NUM_NEIGHBORS) && (dbEntry->node_id != my_node_id)) {
            neighborInfo->neighbors[neighborInfo->neighbors_count].node_id = dbEntry->node_id;
            neighborInfo->neighbors[neighborInfo->neighbors_count].snr = dbEntry->snr;
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
@returns new number of neighbors
*/
size_t NeighborInfoModule::cleanUpNeighbors()
{
    uint32_t now = getTime();
    int num_neighbors = getNumNeighbors();
    NodeNum my_node_id = nodeDB->getNodeNum();

    // Find neighbors to remove
    std::vector<int> indices_to_remove;
    for (int i = 0; i < num_neighbors; i++) {
        const meshtastic_Neighbor *dbEntry = getNeighborByIndex(i);
        // We will remove a neighbor if we haven't heard from them in twice the broadcast interval
        if ((now - dbEntry->last_rx_time > dbEntry->node_broadcast_interval_secs * 2) && (dbEntry->node_id != my_node_id)) {
            indices_to_remove.push_back(i);
        }
    }

    // Update the neighbor list
    for (uint i = 0; i < indices_to_remove.size(); i++) {
        int index = indices_to_remove[i];
        LOG_DEBUG("Removing neighbor with node ID 0x%x\n", neighbors[index].node_id);
        for (int j = index; j < num_neighbors - 1; j++) {
            neighbors[j] = neighbors[j + 1];
        }
        (*numNeighbors)--;
    }

    // Save the neighbor list if we removed any neighbors
    if (indices_to_remove.size() > 0) {
        saveProtoForModule();
    }

    return *numNeighbors;
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
    printNeighborInfo("SENDING", &neighborInfo);
    service.sendToMesh(p, RX_SRC_LOCAL, true);
}

/*
Encompasses the full construction and sending packet to mesh
Will be used for broadcast.
*/
int32_t NeighborInfoModule::runOnce()
{
    bool requestReplies = false;
    sendNeighborInfo(NODENUM_BROADCAST, requestReplies);
    return Default::getConfiguredOrDefaultMs(moduleConfig.neighbor_info.update_interval, default_broadcast_interval_secs);
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
    *numNeighbors = 0;
    neighborState.neighbors_count = 0;
    memset(neighborState.neighbors, 0, sizeof(neighborState.neighbors));
    saveProtoForModule();
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
    for (int i = 0; i < (*numNeighbors); i++) {
        meshtastic_Neighbor *nbr = &neighbors[i];
        if (nbr->node_id == n) {
            // if found, update it
            nbr->snr = snr;
            nbr->last_rx_time = getTime();
            // Only if this is the original sender, the broadcast interval corresponds to it
            if (originalSender == n && node_broadcast_interval_secs != 0)
                nbr->node_broadcast_interval_secs = node_broadcast_interval_secs;
            saveProtoForModule(); // Save the updated neighbor
            return nbr;
        }
    }
    // otherwise, allocate one and assign data to it
    // TODO: max memory for the database should take neighbors into account, but currently doesn't
    if (*numNeighbors < MAX_NUM_NEIGHBORS) {
        (*numNeighbors)++;
    }
    meshtastic_Neighbor *new_nbr = &neighbors[((*numNeighbors) - 1)];
    new_nbr->node_id = n;
    new_nbr->snr = snr;
    new_nbr->last_rx_time = getTime();
    // Only if this is the original sender, the broadcast interval corresponds to it
    if (originalSender == n && node_broadcast_interval_secs != 0)
        new_nbr->node_broadcast_interval_secs = node_broadcast_interval_secs;
    else // Assume the same broadcast interval as us for the neighbor if we don't know it
        new_nbr->node_broadcast_interval_secs = moduleConfig.neighbor_info.update_interval;
    saveProtoForModule(); // Save the new neighbor
    return new_nbr;
}

void NeighborInfoModule::loadProtoForModule()
{
    if (!nodeDB->loadProto(neighborInfoConfigFile, meshtastic_NeighborInfo_size, sizeof(meshtastic_NeighborInfo),
                           &meshtastic_NeighborInfo_msg, &neighborState)) {
        neighborState = meshtastic_NeighborInfo_init_zero;
    }
}

/**
 * @brief Save the module config to file.
 *
 * @return true On success.
 * @return false On error.
 */
bool NeighborInfoModule::saveProtoForModule()
{
    bool okay = true;

#ifdef FS
    FS.mkdir("/prefs");
#endif

    okay &= nodeDB->saveProto(neighborInfoConfigFile, meshtastic_NeighborInfo_size, &meshtastic_NeighborInfo_msg, &neighborState);

    return okay;
}