#pragma once

#include "Observer.h"
#include <Arduino.h>
#include <assert.h>

#include "MeshTypes.h"
#include "NodeStatus.h"
#include "mesh-pb-constants.h"

extern DeviceState devicestate;
extern MyNodeInfo &myNodeInfo;
extern RadioConfig &radioConfig;
extern ChannelSettings &channelSettings;
extern User &owner;

/// Given a node, return how many seconds in the past (vs now) that we last heard from it
uint32_t sinceLastSeen(const NodeInfo *n);

class NodeDB
{
    // NodeNum provisionalNodeNum; // if we are trying to find a node num this is our current attempt

    // A NodeInfo for every node we've seen
    // Eventually use a smarter datastructure
    // HashMap<NodeNum, NodeInfo> nodes;
    // Note: these two references just point into our static array we serialize to/from disk
    NodeInfo *nodes;
    pb_size_t *numNodes;

    uint32_t readPointer = 0;

  public:
    bool updateGUI = false;            // we think the gui should definitely be redrawn, screen will clear this once handled
    NodeInfo *updateGUIforNode = NULL; // if currently showing this node, we think you should update the GUI
    bool updateTextMessage = false;    // if true, the GUI should show a new text message
    Observable<const meshtastic::NodeStatus *> newStatus;

    /// don't do mesh based algoritm for node id assignment (initially)
    /// instead just store in flash - possibly even in the initial alpha release do this hack
    NodeDB();

    /// Called from service after app start, to do init which can only be done after OS load
    void init();

    /// write to flash
    void saveToDisk();

    // Reinit radio config if needed, because sometimes a buggy android app might send us bogus settings
    void resetRadioConfig();

    /// given a subpacket sniffed from the network, update our DB state
    /// we updateGUI and updateGUIforNode if we think our this change is big enough for a redraw
    void updateFrom(const MeshPacket &p);

    /// @return our node number
    NodeNum getNodeNum() { return myNodeInfo.my_node_num; }

    size_t getNumNodes() { return *numNodes; }

    /// if returns false, that means our node should send a DenyNodeNum response.  If true, we think the number is okay for use
    // bool handleWantNodeNum(NodeNum n);

    /* void handleDenyNodeNum(NodeNum FIXME read mesh proto docs, perhaps picking a random node num is not a great idea
    and instead we should use a special 'im unconfigured node number' and include our desired node number in the wantnum message.
    the unconfigured node num would only be used while initially joining the mesh so low odds of conflicting (especially if we
    randomly select from a small number of nodenums which can be used temporarily for this operation).  figure out what the lower
    level mesh sw does if it does conflict?  would it be better for people who are replying with denynode num to just broadcast
    their denial?)
    */

    /// Called from bluetooth when the user wants to start reading the node DB from scratch.
    void resetReadPointer() { readPointer = 0; }

    /// Allow the bluetooth layer to read our next nodeinfo record, or NULL if done reading
    const NodeInfo *readNextInfo();

    /// pick a provisional nodenum we hope no one is using
    void pickNewNodeNum();

    /// Find a node in our DB, return null for missing
    NodeInfo *getNode(NodeNum n);

    NodeInfo *getNodeByIndex(size_t x)
    {
        assert(x < *numNodes);
        return &nodes[x];
    }

    /// Return the number of nodes we've heard from recently (within the last 2 hrs?)
    size_t getNumOnlineNodes();

  private:
    /// Find a node in our DB, create an empty NodeInfo if missing
    NodeInfo *getOrCreateNode(NodeNum n);

    /// Notify observers of changes to the DB
    void notifyObservers(bool forceUpdate = false)
    {
        // Notify observers of the current node state
        const meshtastic::NodeStatus status = meshtastic::NodeStatus(getNumOnlineNodes(), getNumNodes(), forceUpdate);
        newStatus.notifyObservers(&status);
    }

    /// read our db from flash
    void loadFromDisk();

    /// Reinit device state from scratch (not loading from disk)
    void installDefaultDeviceState();
};

/**
 * The node number the user is currently looking at
 * 0 if none
 */
extern NodeNum displayedNodeNum;

extern NodeDB nodeDB;

/**
 * Generate a short suffix used to disambiguate channels that might have the same "name" entered by the human but different PSKs.
 * The ideas is that the PSK changing should be visible to the user so that they see they probably messed up and that's why they
their nodes
 * aren't talking to each other.
 *
 * This string is of the form "#name-XY".
 *
 * Where X is a letter from A to Z (base26), and formed by xoring all the bytes of the PSK together.
 * Y is not yet used but should eventually indicate 'speed/range' of the link
 *
 * This function will also need to be implemented in GUI apps that talk to the radio.
 *
 * https://github.com/meshtastic/Meshtastic-device/issues/269
 */
const char *getChannelName();