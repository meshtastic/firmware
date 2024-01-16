#pragma once

#include "Observer.h"
#include <Arduino.h>
#include <assert.h>

#include "MeshTypes.h"
#include "NodeStatus.h"
#include "mesh-pb-constants.h"
#include "mesh/generated/meshtastic/mesh.pb.h" // For CriticalErrorCode

/*
DeviceState versions used to be defined in the .proto file but really only this function cares.  So changed to a
#define here.
*/

#define SEGMENT_CONFIG 1
#define SEGMENT_MODULECONFIG 2
#define SEGMENT_DEVICESTATE 4
#define SEGMENT_CHANNELS 8

#define DEVICESTATE_CUR_VER 22
#define DEVICESTATE_MIN_VER DEVICESTATE_CUR_VER

extern meshtastic_DeviceState devicestate;
extern meshtastic_ChannelFile channelFile;
extern meshtastic_MyNodeInfo &myNodeInfo;
extern meshtastic_LocalConfig config;
extern meshtastic_LocalModuleConfig moduleConfig;
extern meshtastic_OEMStore oemStore;
extern meshtastic_User &owner;
extern meshtastic_Position localPosition;

/// Given a node, return how many seconds in the past (vs now) that we last heard from it
uint32_t sinceLastSeen(const meshtastic_NodeInfoLite *n);

/// Given a packet, return how many seconds in the past (vs now) it was received
uint32_t sinceReceived(const meshtastic_MeshPacket *p);

class NodeDB
{
    // NodeNum provisionalNodeNum; // if we are trying to find a node num this is our current attempt

    // A NodeInfo for every node we've seen
    // Eventually use a smarter datastructure
    // HashMap<NodeNum, NodeInfo> nodes;
    // Note: these two references just point into our static array we serialize to/from disk
    meshtastic_NodeInfoLite *meshNodes;
    pb_size_t *numMeshNodes;

  public:
    bool updateGUI = false; // we think the gui should definitely be redrawn, screen will clear this once handled
    meshtastic_NodeInfoLite *updateGUIforNode = NULL; // if currently showing this node, we think you should update the GUI
    Observable<const meshtastic::NodeStatus *> newStatus;

    /// don't do mesh based algorithm for node id assignment (initially)
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

    /** Update user info and channel for this node based on received user data
     */
    bool updateUser(uint32_t nodeId, const meshtastic_User &p, uint8_t channelIndex = 0);

    /// @return our node number
    NodeNum getNodeNum() { return myNodeInfo.my_node_num; }

    /// if returns false, that means our node should send a DenyNodeNum response.  If true, we think the number is okay for use
    // bool handleWantNodeNum(NodeNum n);

    /* void handleDenyNodeNum(NodeNum FIXME read mesh proto docs, perhaps picking a random node num is not a great idea
    and instead we should use a special 'im unconfigured node number' and include our desired node number in the wantnum message.
    the unconfigured node num would only be used while initially joining the mesh so low odds of conflicting (especially if we
    randomly select from a small number of nodenums which can be used temporarily for this operation).  figure out what the lower
    level mesh sw does if it does conflict?  would it be better for people who are replying with denynode num to just broadcast
    their denial?)
    */

    /// pick a provisional nodenum we hope no one is using
    void pickNewNodeNum();

    // get channel channel index we heard a nodeNum on, defaults to 0 if not found
    uint8_t getMeshNodeChannel(NodeNum n);

    /// Return the number of nodes we've heard from recently (within the last 2 hrs?)
    size_t getNumOnlineMeshNodes();

    void initConfigIntervals(), initModuleConfigIntervals(), resetNodes(), removeNodeByNum(uint nodeNum);

    bool factoryReset();

    bool loadProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields, void *dest_struct);
    bool saveProto(const char *filename, size_t protoSize, const pb_msgdesc_t *fields, const void *dest_struct);

    void installRoleDefaults(meshtastic_Config_DeviceConfig_Role role);

    const meshtastic_NodeInfoLite *readNextMeshNode(uint32_t &readIndex);

    meshtastic_NodeInfoLite *getMeshNodeByIndex(size_t x)
    {
        assert(x < *numMeshNodes);
        return &meshNodes[x];
    }

    meshtastic_NodeInfoLite *getMeshNode(NodeNum n);
    size_t getNumMeshNodes() { return *numMeshNodes; }

    void setLocalPosition(meshtastic_Position position)
    {
        LOG_DEBUG("Setting local position: latitude=%i, longitude=%i, time=%i\n", position.latitude_i, position.longitude_i,
                  position.time);
        localPosition = position;
    }

  private:
    /// Find a node in our DB, create an empty NodeInfoLite if missing
    meshtastic_NodeInfoLite *getOrCreateMeshNode(NodeNum n);

    /// Notify observers of changes to the DB
    void notifyObservers(bool forceUpdate = false)
    {
        // Notify observers of the current node state
        const meshtastic::NodeStatus status = meshtastic::NodeStatus(getNumOnlineMeshNodes(), getNumMeshNodes(), forceUpdate);
        newStatus.notifyObservers(&status);
    }

    /// read our db from flash
    void loadFromDisk();

    /// purge db entries without user info
    void cleanupMeshDB();

    /// Reinit device state from scratch (not loading from disk)
    void installDefaultDeviceState(), installDefaultChannels(), installDefaultConfig(), installDefaultModuleConfig();
};

extern NodeDB nodeDB;

/*
  If is_router is set, we use a number of different default values

        # FIXME - after tuning, move these params into the on-device defaults based on is_router and is_power_saving

        # prefs.position_broadcast_secs = FIXME possibly broadcast only once an hr
        prefs.wait_bluetooth_secs = 1  # Don't stay in bluetooth mode
        # try to stay in light sleep one full day, then briefly wake and sleep again

        prefs.ls_secs = oneday

        prefs.position_broadcast_secs = 12 hours # send either position or owner every 12hrs

        # get a new GPS position once per day
        prefs.gps_update_interval = oneday

        prefs.is_power_saving = True
*/

// Our delay functions check for this for times that should never expire
#define NODE_DELAY_FOREVER 0xffffffff

#define IF_ROUTER(routerVal, normalVal)                                                                                          \
    ((config.device.role == meshtastic_Config_DeviceConfig_Role_ROUTER) ? (routerVal) : (normalVal))

#define ONE_DAY 24 * 60 * 60

#define default_gps_update_interval IF_ROUTER(ONE_DAY, 2 * 60)
#define default_broadcast_interval_secs IF_ROUTER(ONE_DAY / 2, 15 * 60)
#define default_wait_bluetooth_secs IF_ROUTER(1, 60)
#define default_sds_secs IF_ROUTER(ONE_DAY, UINT32_MAX) // Default to forever super deep sleep
#define default_ls_secs IF_ROUTER(ONE_DAY, 5 * 60)
#define default_min_wake_secs 10
#define default_screen_on_secs IF_ROUTER(1, 60 * 10)

#define default_mqtt_address "mqtt.meshtastic.org"
#define default_mqtt_username "meshdev"
#define default_mqtt_password "large4cats"
#define default_mqtt_root "msh"

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

inline uint32_t getConfiguredOrDefault(uint32_t configured, uint32_t defaultValue)
{
    if (configured > 0)
        return configured;

    return defaultValue;
}

/// Sometimes we will have Position objects that only have a time, so check for
/// valid lat/lon
static inline bool hasValidPosition(const meshtastic_NodeInfoLite *n)
{
    return n->has_position && (n->position.latitude_i != 0 || n->position.longitude_i != 0);
}

/** The current change # for radio settings.  Starts at 0 on boot and any time the radio settings
 * might have changed is incremented.  Allows others to detect they might now be on a new channel.
 */
extern uint32_t radioGeneration;

extern meshtastic_CriticalErrorCode error_code;

/*
 * A numeric error address (nonzero if available)
 */
extern uint32_t error_address;

#define Module_Config_size                                                                                                       \
    (ModuleConfig_CannedMessageConfig_size + ModuleConfig_ExternalNotificationConfig_size + ModuleConfig_MQTTConfig_size +       \
     ModuleConfig_RangeTestConfig_size + ModuleConfig_SerialConfig_size + ModuleConfig_StoreForwardConfig_size +                 \
     ModuleConfig_TelemetryConfig_size + ModuleConfig_size)
