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

#define SEGMENT_CONFIG 1
#define SEGMENT_MODULECONFIG 2
#define SEGMENT_DEVICESTATE 4
#define SEGMENT_CHANNELS 8

#define DEVICESTATE_CUR_VER 20
#define DEVICESTATE_MIN_VER DEVICESTATE_CUR_VER

extern meshtastic_DeviceState devicestate;
extern meshtastic_ChannelFile channelFile;
extern meshtastic_MyNodeInfo &myNodeInfo;
extern meshtastic_LocalConfig config;
extern meshtastic_LocalModuleConfig moduleConfig;
extern meshtastic_OEMStore oemStore;
extern meshtastic_User &owner;

/// Given a node, return how many seconds in the past (vs now) that we last heard from it
uint32_t sinceLastSeen(const meshtastic_NodeInfo *n);

/// Given a packet, return how many seconds in the past (vs now) it was received
uint32_t sinceReceived(const meshtastic_MeshPacket *p);

class NodeDB
{
    // NodeNum provisionalNodeNum; // if we are trying to find a node num this is our current attempt

    // A NodeInfo for every node we've seen
    // Eventually use a smarter datastructure
    // HashMap<NodeNum, NodeInfo> nodes;
    // Note: these two references just point into our static array we serialize to/from disk
    meshtastic_NodeInfo *nodes;
    pb_size_t *numNodes;

    uint32_t readPointer = 0;

  public:
    bool updateGUI = false; // we think the gui should definitely be redrawn, screen will clear this once handled
    meshtastic_NodeInfo *updateGUIforNode = NULL; // if currently showing this node, we think you should update the GUI
    Observable<const meshtastic::NodeStatus *> newStatus;

    /// don't do mesh based algoritm for node id assignment (initially)
    /// instead just store in flash - possibly even in the initial alpha release do this hack
    NodeDB();

    /// Called from service after app start, to do init which can only be done after OS load
    void init();

    /// write to flash
    void saveToDisk(int saveWhat = SEGMENT_CONFIG | SEGMENT_MODULECONFIG | SEGMENT_DEVICESTATE | SEGMENT_CHANNELS),
        saveChannelsToDisk(), saveDeviceStateToDisk();

    /** Reinit radio config if needed, because either:
     * a) sometimes a buggy android app might send us bogus settings or
     * b) the client set factory_reset
     *
     * @return true if the config was completely reset, in that case, we should send it back to the client
     */
    bool resetRadioConfig(bool factory_reset = false);

    /// given a subpacket sniffed from the network, update our DB state
    /// we updateGUI and updateGUIforNode if we think our this change is big enough for a redraw
    void updateFrom(const meshtastic_MeshPacket &p);

    /** Update position info for this node based on received position data
     */
    void updatePosition(uint32_t nodeId, const meshtastic_Position &p, RxSource src = RX_SRC_RADIO);

    /** Update telemetry info for this node based on received metrics
     */
    void updateTelemetry(uint32_t nodeId, const meshtastic_Telemetry &t, RxSource src = RX_SRC_RADIO);

    /** Update user info for this node based on received user data
     */
    void updateUser(uint32_t nodeId, const meshtastic_User &p);

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
    const meshtastic_NodeInfo *readNextInfo();

    /// pick a provisional nodenum we hope no one is using
    void pickNewNodeNum();

    /// Find a node in our DB, return null for missing
    meshtastic_NodeInfo *getNode(NodeNum n);

    meshtastic_NodeInfo *getNodeByIndex(size_t x)
    {
        assert(x < *numNodes);
        return &nodes[x];
    }

    /// Return the number of nodes we've heard from recently (within the last 2 hrs?)
    size_t getNumOnlineNodes();

    void initConfigIntervals(), initModuleConfigIntervals(), resetNodes();

    bool factoryReset();

    bool loadProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields, void *dest_struct);
    bool saveProto(const char *filename, size_t protoSize, const pb_msgdesc_t *fields, const void *dest_struct);

    void installRoleDefaults(meshtastic_Config_DeviceConfig_Role role);

  private:
    /// Find a node in our DB, create an empty NodeInfo if missing
    meshtastic_NodeInfo *getOrCreateNode(NodeNum n);

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
    void installDefaultDeviceState(), installDefaultChannels(), installDefaultConfig(), installDefaultModuleConfig();
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

#define IF_ROUTER(routerVal, normalVal)                                                                                          \
    ((config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER) ? (routerVal) : (normalVal))

#define ONE_DAY 24 * 60 * 60

#define default_gps_attempt_time IF_ROUTER(5 * 60, 15 * 60)
#define default_gps_update_interval IF_ROUTER(ONE_DAY, 2 * 60)
#define default_broadcast_interval_secs IF_ROUTER(ONE_DAY / 2, 15 * 60)
#define default_wait_bluetooth_secs IF_ROUTER(1, 60)
#define default_mesh_sds_timeout_secs IF_ROUTER(NODE_DELAY_FOREVER, 2 * 60 * 60)
#define default_sds_secs IF_ROUTER(ONE_DAY, UINT32_MAX) // Default to forever super deep sleep
#define default_ls_secs IF_ROUTER(ONE_DAY, 5 * 60)
#define default_min_wake_secs 10
#define default_screen_on_secs 60 * 10

#define default_mqtt_address "mqtt.meshtastic.org"
#define default_mqtt_username "meshdev"
#define default_mqtt_password "large4cats"

inline uint32_t getConfiguredOrDefaultMs(uint32_t configuredInterval)
{
    if (configuredInterval > 0)
        return configuredInterval * 1000;
    return default_broadcast_interval_secs * 1000;
}

inline uint32_t getConfiguredOrDefaultMs(uint32_t configuredInterval, uint32_t defaultInterval)
{
    if (configuredInterval > 0)
        return configuredInterval * 1000;
    return defaultInterval * 1000;
}

/** The current change # for radio settings.  Starts at 0 on boot and any time the radio settings
 * might have changed is incremented.  Allows others to detect they might now be on a new channel.
 */
extern uint32_t radioGeneration;

#define Module_Config_size                                                                                                       \
    (ModuleConfig_CannedMessageConfig_size + ModuleConfig_ExternalNotificationConfig_size + ModuleConfig_MQTTConfig_size +       \
     ModuleConfig_RangeTestConfig_size + ModuleConfig_SerialConfig_size + ModuleConfig_StoreForwardConfig_size +                 \
     ModuleConfig_TelemetryConfig_size + ModuleConfig_size)
