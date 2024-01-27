#pragma once
#include "ProtobufModule.h"

/*
 * Neighborinfo module for sending info on each node's 0-hop neighbors to the mesh
 */
class NeighborInfoModule : public ProtobufModule<meshtastic_NeighborInfo>, private concurrency::OSThread
{
    meshtastic_Neighbor *neighbors;
    pb_size_t *numNeighbors;

  public:
    /*
     * Expose the constructor
     */
    NeighborInfoModule();

    /* Reset neighbor info after clearing nodeDB*/
    void resetNeighbors();

    bool saveProtoForModule();

    // Let FloodingRouter call updateLastSentById upon rebroadcasting a NeighborInfo packet
    friend class FloodingRouter;

  protected:
    // Note: this holds our local info.
    meshtastic_NeighborInfo neighborState;

    /*
     * Called to handle a particular incoming message
     * @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
     */
    virtual bool handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_NeighborInfo *nb) override;

    /*
     * Collect neighbor info from the nodeDB's history, capping at a maximum number of entries and max time
     * @return the number of entries collected
     */
    uint32_t collectNeighborInfo(meshtastic_NeighborInfo *neighborInfo);

    /*
    Remove neighbors from the database that we haven't heard from in a while
    @returns new number of neighbors
    */
    size_t cleanUpNeighbors();

    /* Allocate a new NeighborInfo packet */
    meshtastic_NeighborInfo *allocateNeighborInfoPacket();

    // Find a neighbor in our DB, create an empty neighbor if missing
    meshtastic_Neighbor *getOrCreateNeighbor(NodeNum originalSender, NodeNum n, uint32_t node_broadcast_interval_secs, float snr);

    /*
     * Send info on our node's neighbors into the mesh
     */
    void sendNeighborInfo(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);

    size_t getNumNeighbors() { return *numNeighbors; }

    meshtastic_Neighbor *getNeighborByIndex(size_t x)
    {
        assert(x < *numNeighbors);
        return &neighbors[x];
    }

    /* update neighbors with subpacket sniffed from network */
    void updateNeighbors(const meshtastic_MeshPacket &mp, const meshtastic_NeighborInfo *np);

    /* update a NeighborInfo packet with our NodeNum as last_sent_by_id */
    void updateLastSentById(meshtastic_MeshPacket *p);

    void loadProtoForModule();

    /* Does our periodic broadcast */
    int32_t runOnce() override;

    /* These are for debugging only */
    void printNeighborInfo(const char *header, const meshtastic_NeighborInfo *np);
    void printNodeDBNodes(const char *header);
    void printNodeDBNeighbors(const char *header);
    void printNodeDBSelection(const char *header, const meshtastic_NeighborInfo *np);
};
extern NeighborInfoModule *neighborInfoModule;