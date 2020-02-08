#pragma once

#include <Arduino.h>
#include <assert.h>

#include "mesh-pb-constants.h"
#include "MeshTypes.h"

extern MyNodeInfo &myNodeInfo;
extern RadioConfig &radioConfig;
extern User &owner;

class NodeDB
{
    // NodeNum provisionalNodeNum; // if we are trying to find a node num this is our current attempt

    // A NodeInfo for every node we've seen
    // Eventually use a smarter datastructure
    // HashMap<NodeNum, NodeInfo> nodes;
    // Note: these two references just point into our static array we serialize to/from disk
    NodeInfo *nodes;
    pb_size_t *numNodes;

    bool updateGUI = false;             // we think the gui should definitely be redrawn
    NodeInfo *updateGUIforNode = NULL; // if currently showing this node, we think you should update the GUI

    int readPointer = 0;

public:
    /// don't do mesh based algoritm for node id assignment (initially)
    /// instead just store in flash - possibly even in the initial alpha release do this hack
    NodeDB();

    /// Called from service after app start, to do init which can only be done after OS load
    void init();

    /// write to flash
    void saveToDisk();

    /// given a subpacket sniffed from the network, update our DB state
    /// we updateGUI and updateGUIforNode if we think our this change is big enough for a redraw
    void updateFrom(const MeshPacket &p);

    NodeNum getNodeNum() { return myNodeInfo.my_node_num; }

    /// if returns false, that means our node should send a DenyNodeNum response.  If true, we think the number is okay for use
    // bool handleWantNodeNum(NodeNum n);

    /* void handleDenyNodeNum(NodeNum FIXME read mesh proto docs, perhaps picking a random node num is not a great idea
    and instead we should use a special 'im unconfigured node number' and include our desired node number in the wantnum message.  the
    unconfigured node num would only be used while initially joining the mesh so low odds of conflicting (especially if we randomly select
    from a small number of nodenums which can be used temporarily for this operation).  figure out what the lower level
    mesh sw does if it does conflict?  would it be better for people who are replying with denynode num to just broadcast their denial?)
    */

    /// Called from bluetooth when the user wants to start reading the node DB from scratch.
    void resetReadPointer() { readPointer = 0; }

    /// Allow the bluetooth layer to read our next nodeinfo record, or NULL if done reading
    const NodeInfo *readNextInfo();

    /// pick a provisional nodenum we hope no one is using
    void pickNewNodeNum();

private:
    /// Find a node in our DB, return null for missing
    NodeInfo *getNode(NodeNum n);

    /// Find a node in our DB, create an empty NodeInfo if missing
    NodeInfo *getOrCreateNode(NodeNum n);

    /// read our db from flash
    void loadFromDisk();
};

extern NodeDB nodeDB;

