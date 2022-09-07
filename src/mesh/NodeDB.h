#pragma once

#include "Observer.h"
#include <Arduino.h>
#include <assert.h>

#include "MeshTypes.h"
#include "NodeStatus.h"
#include "mesh-pb-constants.h"

/*
DeviceState versions used to be defined in the .proto file but really only this function cares.  So changed to a
#define here.
*/

#define DEVICESTATE_CUR_VER 16
#define DEVICESTATE_MIN_VER DEVICESTATE_CUR_VER

extern DeviceState devicestate;
extern ChannelFile channelFile;
extern MyNodeInfo &myNodeInfo;
extern LocalConfig config;
extern LocalModuleConfig moduleConfig;
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
    Observable<const meshtastic::NodeStatus *> newStatus;

    /// don't do mesh based algoritm for node id assignment (initially)
    /// instead just store in flash - possibly even in the initial alpha release do this hack
    NodeDB();

    /// Called from service after app start, to do init which can only be done after OS load
    void init();

    /// write to flash
    void saveToDisk(), saveChannelsToDisk(), saveDeviceStateToDisk();

    /** Reinit radio config if needed, because either:
     * a) sometimes a buggy android app might send us bogus settings or
     * b) the client set factory_reset
     *
     * @return true if the config was completely reset, in that case, we should send it back to the client
     */
    bool resetRadioConfig();

    /// given a subpacket sniffed from the network, update our DB state
    /// we updateGUI and updateGUIforNode if we think our this change is big enough for a redraw
    void updateFrom(const MeshPacket &p);

    /** Update position info for this node based on received position data
     */
    void updatePosition(uint32_t nodeId, const Position &p, RxSource src = RX_SRC_RADIO);

    /** Update telemetry info for this node based on received metrics
     */
    void updateTelemetry(uint32_t nodeId, const Telemetry &t, RxSource src = RX_SRC_RADIO);

    /** Update user info for this node based on received user data
     */
    void updateUser(uint32_t nodeId, const User &p);

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

    bool factoryReset();

    /// read our db from flash
    void loadFromDisk();

    /// Reinit device state from scratch (not loading from disk)
    void installDefaultDeviceState(), installDefaultChannels(), installDefaultConfig(),
        installDefaultModuleConfig();
};

/**
 * The node number the user is currently looking at
 * 0 if none
 */
extern NodeNum displayedNodeNum;

extern NodeDB nodeDB;

/*
  If is_router is set, we use a number of different default values

        # FIXME - after tuning, move these params into the on-device defaults based on is_router and is_power_saving

        # prefs.position_broadcast_secs = FIXME possibly broadcast only once an hr
        prefs.wait_bluetooth_secs = 1  # Don't stay in bluetooth mode
        prefs.mesh_sds_timeout_secs = never
        # try to stay in light sleep one full day, then briefly wake and sleep again

        prefs.ls_secs = oneday

        prefs.position_broadcast_secs = 12 hours # send either position or owner every 12hrs

        # get a new GPS position once per day
        prefs.gps_update_interval = oneday

        prefs.is_power_saving = True

        # allow up to five minutes for each new GPS lock attempt
        prefs.gps_attempt_time = 300
*/

// Our delay functions check for this for times that should never expire
#define NODE_DELAY_FOREVER 0xffffffff

#define IF_ROUTER(routerVal, normalVal) ((config.device.role == Config_DeviceConfig_Role_Router) ? (routerVal) : (normalVal))

#define default_broadcast_interval_secs IF_ROUTER(12 * 60 * 60, 15 * 60)
#define default_wait_bluetooth_secs IF_ROUTER(1, 60)
#define default_mesh_sds_timeout_secs IF_ROUTER(NODE_DELAY_FOREVER, 2 * 60 * 60)
#define default_sds_secs 365 * 24 * 60 * 60
#define default_ls_secs IF_ROUTER(24 * 60 * 60, 5 * 60)
#define default_min_wake_secs 10
#define default_screen_on_secs 60 * 10

inline uint32_t getConfiguredOrDefaultMs(uint32_t configuredInterval)
{
    if (configuredInterval > 0) return configuredInterval * 1000;
    return default_broadcast_interval_secs * 1000;
}

inline uint32_t getConfiguredOrDefaultMs(uint32_t configuredInterval, uint32_t defaultInterval)
{
    if (configuredInterval > 0) return configuredInterval * 1000;
    return defaultInterval * 1000;
}

/** The current change # for radio settings.  Starts at 0 on boot and any time the radio settings
 * might have changed is incremented.  Allows others to detect they might now be on a new channel.
 */
extern uint32_t radioGeneration;

#define Module_Config_size (ModuleConfig_CannedMessageConfig_size + ModuleConfig_ExternalNotificationConfig_size + ModuleConfig_MQTTConfig_size + ModuleConfig_RangeTestConfig_size + ModuleConfig_SerialConfig_size + ModuleConfig_StoreForwardConfig_size + ModuleConfig_TelemetryConfig_size + ModuleConfig_size)