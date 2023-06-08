#include "NeighborInfoModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"

#define MAX_NEIGHBOR_AGE 10 * 60 * 1000 // 10 minutes
#define MAX_NUM_NEIGHBORS 10            // also defined in NeighborInfo protobuf options
NeighborInfoModule *neighborInfoModule;

static const char *neighborInfoConfigFile = "/prefs/neighbors.proto";

/*
Prints a single neighbor info packet and associated neighbors
Uses LOG_DEBUG, which equates to Console.log
NOTE: For debugging only
*/
void NeighborInfoModule::printNeighborInfo(const char *header, const meshtastic_NeighborInfo *np)
{
    LOG_DEBUG("%s NEIGHBORINFO PACKET from Node %d to Node %d (last sent by %d)\n", header, np->node_id, nodeDB.getNodeNum(),
              np->last_sent_by_id);
    LOG_DEBUG("----------------\n");
    LOG_DEBUG("Packet contains %d neighbors\n", np->neighbors_count);
    for (int i = 0; i < np->neighbors_count; i++) {
        LOG_DEBUG("Neighbor %d: node_id=%d, snr=%d\n", i, np->neighbors[i].node_id, np->neighbors[i].snr);
    }
    LOG_DEBUG("----------------\n");
}
/*
Prints the nodeDB nodes so we can see whose nodeInfo we have
NOTE: for debugging only
*/
void NeighborInfoModule::printNodeDBNodes(const char *header)
{
    int num_nodes = nodeDB.getNumNodes();
    LOG_DEBUG("%s NODEDB SELECTION from Node %d:\n", header, nodeDB.getNodeNum());
    LOG_DEBUG("----------------\n");
    LOG_DEBUG("DB contains %d nodes\n", num_nodes);
    for (int i = 0; i < num_nodes; i++) {
        meshtastic_NodeInfo *dbEntry = nodeDB.getNodeByIndex(i);
        LOG_DEBUG("     Node %d: node_id=%d, snr=%.2f\n", i, dbEntry->num, dbEntry->snr);
    }
    LOG_DEBUG("----------------\n");
}

/*
Prints the nodeDB neighbors
NOTE: for debugging only
*/
void NeighborInfoModule::printNodeDBNeighbors(const char *header)
{
    int num_neighbors = getNumNeighbors();
    LOG_DEBUG("%s NODEDB SELECTION from Node %d:\n", header, nodeDB.getNodeNum());
    LOG_DEBUG("----------------\n");
    LOG_DEBUG("DB contains %d neighbors\n", num_neighbors);
    for (int i = 0; i < num_neighbors; i++) {
        meshtastic_Neighbor *dbEntry = getNeighborByIndex(i);
        LOG_DEBUG("     Node %d: node_id=%d, snr=%.2f\n", i, dbEntry->node_id, dbEntry->snr);
    }
    LOG_DEBUG("----------------\n");
}

/*
Prints the nodeDB with selectors for the neighbors we've chosen to send (inefficiently)
Uses LOG_DEBUG, which equates to Console.log
NOTE: For debugging only
*/
void NeighborInfoModule::printNodeDBSelection(const char *header, const meshtastic_NeighborInfo *np)
{
    int num_neighbors = getNumNeighbors();
    LOG_DEBUG("%s NODEDB SELECTION from Node %d:\n", header, nodeDB.getNodeNum());
    LOG_DEBUG("----------------\n");
    LOG_DEBUG("Selected %d neighbors of %d DB neighbors\n", np->neighbors_count, num_neighbors);
    for (int i = 0; i < num_neighbors; i++) {
        meshtastic_Neighbor *dbEntry = getNeighborByIndex(i);
        bool chosen = false;
        for (int j = 0; j < np->neighbors_count; j++) {
            if (np->neighbors[j].node_id == dbEntry->node_id) {
                chosen = true;
            }
        }
        if (!chosen) {
            LOG_DEBUG("     Node %d: neighbor=%d, snr=%.2f\n", i, dbEntry->node_id, dbEntry->snr);
        } else {
            LOG_DEBUG("---> Node %d: neighbor=%d, snr=%.2f\n", i, dbEntry->node_id, dbEntry->snr);
        }
    }
    LOG_DEBUG("----------------\n");
}

/* Send our initial owner announcement 35 seconds after we start (to give network time to setup) */
NeighborInfoModule::NeighborInfoModule()
    : neighbors(neighborState.neighbors), numNeighbors(&neighborState.neighbors_count),
      ProtobufModule("neighborinfo", meshtastic_PortNum_NEIGHBORINFO_APP, &meshtastic_NeighborInfo_msg), concurrency::OSThread(
                                                                                                             "NeighborInfoModule")
{
    ourPortNum = meshtastic_PortNum_NEIGHBORINFO_APP;

    if (moduleConfig.neighbor_info.enabled) {
        this->loadProtoForModule();
        setIntervalFromNow(35 * 1000);
    } else {
        LOG_DEBUG("NeighborInfoModule is disabled\n");
        disable();
    }
}

/*
Allocate a zeroed neighbor info packet
*/
meshtastic_NeighborInfo *NeighborInfoModule::allocateNeighborInfoPacket()
{
    meshtastic_NeighborInfo *neighborInfo = (meshtastic_NeighborInfo *)malloc(sizeof(meshtastic_NeighborInfo));
    memset(neighborInfo, 0, sizeof(meshtastic_NeighborInfo));
    return neighborInfo;
}

/*
Collect neighbor info from the nodeDB's history, capping at a maximum number of entries and max time
Assumes that the neighborInfo packet has been allocated
@returns the number of entries collected
*/
uint32_t NeighborInfoModule::collectNeighborInfo(meshtastic_NeighborInfo *neighborInfo)
{
    int num_neighbors = getNumNeighbors();
    int current_time = getTime();
    int my_node_id = nodeDB.getNodeNum();
    neighborInfo->node_id = my_node_id;
    neighborInfo->last_sent_by_id = my_node_id;

    for (int i = 0; i < num_neighbors; i++) {
        meshtastic_Neighbor *dbEntry = getNeighborByIndex(i);
        if ((neighborInfo->neighbors_count < MAX_NUM_NEIGHBORS) && (dbEntry->node_id != my_node_id)) {
            neighborInfo->neighbors[neighborInfo->neighbors_count].node_id = dbEntry->node_id;
            neighborInfo->neighbors[neighborInfo->neighbors_count].snr = dbEntry->snr;
            neighborInfo->neighbors_count++;
        }
    }
    printNodeDBNodes("DBSTATE");
    printNodeDBNeighbors("NEIGHBORS");
    printNodeDBSelection("COLLECTED", neighborInfo);
    return neighborInfo->neighbors_count;
}

/* Send neighbor info to the mesh */
void NeighborInfoModule::sendNeighborInfo(NodeNum dest, bool wantReplies)
{
    meshtastic_NeighborInfo *neighborInfo = allocateNeighborInfoPacket();
    collectNeighborInfo(neighborInfo);
    meshtastic_MeshPacket *p = allocDataProtobuf(*neighborInfo);
    // send regardless of whether or not we have neighbors in our DB,
    // because we want to get neighbors for the next cycle
    p->to = dest;
    p->decoded.want_response = wantReplies;
    printNeighborInfo("SENDING", neighborInfo);
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
    return getConfiguredOrDefaultMs(moduleConfig.neighbor_info.update_interval, default_broadcast_interval_secs);
}

/*
Collect a recieved neighbor info packet from another node
Pass it to an upper client; do not persist this data on the mesh
*/
bool NeighborInfoModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_NeighborInfo *np)
{
    printNeighborInfo("RECIEVED", np);
    updateNeighbors(mp, np);
    // Allow others to handle this packet
    return false;
}

/*
Copy the content of a current NeighborInfo packet into a new one and update the last_sent_by_id to our NodeNum
*/
void NeighborInfoModule::updateLastSentById(meshtastic_MeshPacket *p)
{
    auto &incoming = p->decoded;
    meshtastic_NeighborInfo scratch;
    meshtastic_NeighborInfo *updated = NULL;
    memset(&scratch, 0, sizeof(scratch));
    pb_decode_from_bytes(incoming.payload.bytes, incoming.payload.size, &meshtastic_NeighborInfo_msg, &scratch);
    updated = &scratch;

    updated->last_sent_by_id = nodeDB.getNodeNum();

    // Set updated last_sent_by_id to the payload of the to be flooded packet
    p->decoded.payload.size =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_NeighborInfo_msg, updated);
}

void NeighborInfoModule::resetNeighbors()
{
    neighborState.neighbors_count = 0;
    memset(neighborState.neighbors, 0, sizeof(neighborState.neighbors));
    saveProtoForModule();
}

void NeighborInfoModule::updateNeighbors(const meshtastic_MeshPacket &mp, meshtastic_NeighborInfo *np)
{
    // The last sent ID will be 0 if the packet is from the phone, which we don't count as
    // an edge. So we assume that if it's zero, then this packet is from our node.
    if (mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag && mp.from) {
        getOrCreateNeighbor(np->last_sent_by_id, mp.rx_snr);
    }
}

meshtastic_Neighbor *NeighborInfoModule::getOrCreateNeighbor(NodeNum n, int snr)
{
    // our node and the phone are the same node (not neighbors)
    if (n == 0) {
        n = nodeDB.getNodeNum();
    }
    // look for one in the existing list
    for (int i = 0; i < (*numNeighbors); i++) {
        meshtastic_Neighbor *nbr = &neighbors[i];
        if (nbr->node_id == n) {
            // if found, update it
            nbr->snr = snr;
            return nbr;
        }
    }
    // otherwise, allocate one and assign data to it
    // TODO: max memory for the database should take neighbors into account, but currently doesn't
    if (*numNeighbors < MAX_NUM_NODES) {
        (*numNeighbors)++;
    }
    meshtastic_Neighbor *new_nbr = &neighbors[((*numNeighbors) - 1)];
    new_nbr->node_id = n;
    new_nbr->snr = snr;
    return new_nbr;
}

void NeighborInfoModule::loadProtoForModule()
{
    if (!nodeDB.loadProto(neighborInfoConfigFile, meshtastic_NeighborInfo_size, sizeof(meshtastic_NeighborInfo),
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

    okay &= nodeDB.saveProto(neighborInfoConfigFile, meshtastic_NeighborInfo_size, &meshtastic_NeighborInfo_msg, &neighborState);

    return okay;
}