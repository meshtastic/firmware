#pragma once

#include "Observer.h"
#include <Arduino.h>
#include <algorithm>
#include <assert.h>
#include <pb_encode.h>
#include <vector>

#include "MeshTypes.h"
#include "NodeStatus.h"
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "mesh/generated/meshtastic/mesh.pb.h" // For CriticalErrorCode

#if ARCH_PORTDUINO
#include "PortduinoGlue.h"
#endif

#if !defined(MESHTASTIC_EXCLUDE_PKI)

static const uint8_t LOW_ENTROPY_HASH1[] = {0xf4, 0x7e, 0xcc, 0x17, 0xe6, 0xb4, 0xa3, 0x22, 0xec, 0xee, 0xd9,
                                            0x08, 0x4f, 0x39, 0x63, 0xea, 0x80, 0x75, 0xe1, 0x24, 0xce, 0x05,
                                            0x36, 0x69, 0x63, 0xb2, 0xcb, 0xc0, 0x28, 0xd3, 0x34, 0x8b};
static const uint8_t LOW_ENTROPY_HASH2[] = {0x5a, 0x9e, 0xa2, 0xa6, 0x8a, 0xa6, 0x66, 0xc1, 0x5f, 0x55, 0x00,
                                            0x64, 0xa3, 0xa6, 0xfe, 0x71, 0xc0, 0xbb, 0x82, 0xc3, 0x32, 0x3d,
                                            0x7a, 0x7a, 0xe3, 0x6e, 0xfd, 0xdd, 0xad, 0x3a, 0x66, 0xb9};
static const uint8_t LOW_ENTROPY_HASH3[] = {0xb3, 0xdf, 0x3b, 0x2e, 0x67, 0xb6, 0xd5, 0xf8, 0xdf, 0x76, 0x2c,
                                            0x45, 0x5e, 0x2e, 0xbd, 0x16, 0xc5, 0xf8, 0x67, 0xaa, 0x15, 0xf8,
                                            0x92, 0x0b, 0xdf, 0x5a, 0x66, 0x50, 0xac, 0x0d, 0xbb, 0x2f};
static const uint8_t LOW_ENTROPY_HASH4[] = {0x3b, 0x8f, 0x86, 0x3a, 0x38, 0x1f, 0x77, 0x39, 0xa9, 0x4e, 0xef,
                                            0x91, 0x18, 0x5a, 0x62, 0xe1, 0xaa, 0x9d, 0x36, 0xea, 0xce, 0x60,
                                            0x35, 0x8d, 0x9d, 0x1f, 0xf4, 0xb8, 0xc9, 0x13, 0x6a, 0x5d};
static const uint8_t LOW_ENTROPY_HASH5[] = {0x36, 0x7e, 0x2d, 0xe1, 0x84, 0x5f, 0x42, 0x52, 0x29, 0x11, 0x0a,
                                            0x25, 0x64, 0x54, 0x6a, 0x6b, 0xfd, 0xb6, 0x65, 0xff, 0x15, 0x1a,
                                            0x51, 0x71, 0x22, 0x40, 0x57, 0xf6, 0x91, 0x9b, 0x64, 0x58};
static const uint8_t LOW_ENTROPY_HASH6[] = {0x16, 0x77, 0xeb, 0xa4, 0x52, 0x91, 0xfb, 0x26, 0xcf, 0x8f, 0xd7,
                                            0xd9, 0xd1, 0x5d, 0xc4, 0x68, 0x73, 0x75, 0xed, 0xc5, 0x95, 0x58,
                                            0xee, 0x90, 0x56, 0xd4, 0x2f, 0x31, 0x29, 0xf7, 0x8c, 0x1f};
static const uint8_t LOW_ENTROPY_HASH7[] = {0x31, 0x8c, 0xa9, 0x5e, 0xed, 0x3c, 0x12, 0xbf, 0x97, 0x9c, 0x47,
                                            0x8e, 0x98, 0x9d, 0xc2, 0x3e, 0x86, 0x23, 0x90, 0x29, 0xc8, 0xb0,
                                            0x20, 0xf8, 0xb1, 0xb0, 0xaa, 0x19, 0x2a, 0xcf, 0x0a, 0x54};
static const uint8_t LOW_ENTROPY_HASH8[] = {0xa4, 0x8a, 0x99, 0x0e, 0x51, 0xdc, 0x12, 0x20, 0xf3, 0x13, 0xf5,
                                            0x2b, 0x3a, 0xe2, 0x43, 0x42, 0xc6, 0x52, 0x98, 0xcd, 0xbb, 0xca,
                                            0xb1, 0x31, 0xa0, 0xd4, 0xd6, 0x30, 0xf3, 0x27, 0xfb, 0x49};
static const uint8_t LOW_ENTROPY_HASH9[] = {0xd2, 0x3f, 0x13, 0x8d, 0x22, 0x04, 0x8d, 0x07, 0x59, 0x58, 0xa0,
                                            0xf9, 0x55, 0xcf, 0x30, 0xa0, 0x2e, 0x2f, 0xca, 0x80, 0x20, 0xe4,
                                            0xde, 0xa1, 0xad, 0xd9, 0x58, 0xb3, 0x43, 0x2b, 0x22, 0x70};
static const uint8_t LOW_ENTROPY_HASH10[] = {0x40, 0x41, 0xec, 0x6a, 0xd2, 0xd6, 0x03, 0xe4, 0x9a, 0x9e, 0xbd,
                                             0x6c, 0x0a, 0x9b, 0x75, 0xa4, 0xbc, 0xab, 0x6f, 0xa7, 0x95, 0xff,
                                             0x2d, 0xf6, 0xe9, 0xb9, 0xab, 0x4c, 0x0c, 0x1c, 0xd0, 0x3b};
static const uint8_t LOW_ENTROPY_HASH11[] = {0x22, 0x49, 0x32, 0x2b, 0x00, 0xf9, 0x22, 0xfa, 0x17, 0x02, 0xe9,
                                             0x64, 0x82, 0xf0, 0x4d, 0x1b, 0xc7, 0x04, 0xfc, 0xdc, 0x8c, 0x5e,
                                             0xb6, 0xd9, 0x16, 0xd6, 0x37, 0xce, 0x59, 0xaa, 0x09, 0x49};
static const uint8_t LOW_ENTROPY_HASH12[] = {0x48, 0x6f, 0x1e, 0x48, 0x97, 0x88, 0x64, 0xac, 0xe8, 0xeb, 0x30,
                                             0xa3, 0xc3, 0xe1, 0xcf, 0x97, 0x39, 0xa6, 0x55, 0x5b, 0x5f, 0xbf,
                                             0x18, 0xb7, 0x3a, 0xdf, 0xa8, 0x75, 0xe7, 0x9d, 0xe0, 0x1e};
static const uint8_t LOW_ENTROPY_HASH13[] = {0x09, 0xb4, 0xe2, 0x6d, 0x28, 0x98, 0xc9, 0x47, 0x66, 0x46, 0xbf,
                                             0xff, 0x58, 0x17, 0x91, 0xaa, 0xc3, 0xbf, 0x4a, 0x9d, 0x0b, 0x88,
                                             0xb1, 0xf1, 0x03, 0xdd, 0x61, 0xd7, 0xba, 0x9e, 0x64, 0x98};
static const uint8_t LOW_ENTROPY_HASH14[] = {0x39, 0x39, 0x84, 0xe0, 0x22, 0x2f, 0x7d, 0x78, 0x45, 0x18, 0x72,
                                             0xb4, 0x13, 0xd2, 0x01, 0x2f, 0x3c, 0xa1, 0xb0, 0xfe, 0x39, 0xd0,
                                             0xf1, 0x3c, 0x72, 0xd6, 0xef, 0x54, 0xd5, 0x77, 0x22, 0xa0};
static const uint8_t LOW_ENTROPY_HASH15[] = {0x0a, 0xda, 0x5f, 0xec, 0xff, 0x5c, 0xc0, 0x2e, 0x5f, 0xc4, 0x8d,
                                             0x03, 0xe5, 0x80, 0x59, 0xd3, 0x5d, 0x49, 0x86, 0xe9, 0x8d, 0xf6,
                                             0xf6, 0x16, 0x35, 0x3d, 0xf9, 0x9b, 0x29, 0x55, 0x9e, 0x64};
static const uint8_t LOW_ENTROPY_HASH16[] = {0x08, 0x56, 0xF0, 0xD7, 0xEF, 0x77, 0xD6, 0x11, 0x1C, 0x8F, 0x95,
                                             0x2D, 0x3C, 0xDF, 0xB1, 0x22, 0xBF, 0x60, 0x9B, 0xE5, 0xA9, 0xC0,
                                             0x6E, 0x4B, 0x01, 0xDC, 0xD1, 0x57, 0x44, 0xB2, 0xA5, 0xCF};
static const uint8_t LOW_ENTROPY_HASH17[] = {0x2C, 0xB2, 0x77, 0x85, 0xD6, 0xB7, 0x48, 0x9C, 0xFE, 0xBC, 0x80,
                                             0x26, 0x60, 0xF4, 0x6D, 0xCE, 0x11, 0x31, 0xA2, 0x1E, 0x33, 0x0A,
                                             0x6D, 0x2B, 0x00, 0xFA, 0x0C, 0x90, 0x95, 0x8F, 0x5C, 0x6B};
static const uint8_t LOW_ENTROPY_HASH18[] = {0xFA, 0x59, 0xC8, 0x6E, 0x94, 0xEE, 0x75, 0xC9, 0x9A, 0xB0, 0xFE,
                                             0x89, 0x36, 0x40, 0xC9, 0x99, 0x4A, 0x3B, 0xF4, 0xAA, 0x12, 0x24,
                                             0xA2, 0x0F, 0xF9, 0xD1, 0x08, 0xCB, 0x78, 0x19, 0xAA, 0xE5};
static const uint8_t LOW_ENTROPY_HASH19[] = {0x6E, 0x42, 0x7A, 0x4A, 0x8C, 0x61, 0x62, 0x22, 0xA1, 0x89, 0xD3,
                                             0xA4, 0xC2, 0x19, 0xA3, 0x83, 0x53, 0xA7, 0x7A, 0x0A, 0x89, 0xE2,
                                             0x54, 0x52, 0x62, 0x3D, 0xE7, 0xCA, 0x8C, 0xF6, 0x6A, 0x60};
static const uint8_t LOW_ENTROPY_HASH20[] = {0x20, 0x27, 0x2F, 0xBA, 0x0C, 0x99, 0xD7, 0x29, 0xF3, 0x11, 0x35,
                                             0x89, 0x9D, 0x0E, 0x24, 0xA1, 0xC3, 0xCB, 0xDF, 0x8A, 0xF1, 0xC6,
                                             0xFE, 0xD0, 0xD7, 0x9F, 0x92, 0xD6, 0x8F, 0x59, 0xBF, 0xE4};
static const char LOW_ENTROPY_WARNING[] = "Compromised keys detected, please regenerate.";
#endif
/*
DeviceState versions used to be defined in the .proto file but really only this function cares.  So changed to a
#define here.
*/

#define SEGMENT_CONFIG 1
#define SEGMENT_MODULECONFIG 2
#define SEGMENT_DEVICESTATE 4
#define SEGMENT_CHANNELS 8
#define SEGMENT_NODEDATABASE 16

#define DEVICESTATE_CUR_VER 24
#define DEVICESTATE_MIN_VER 24

extern meshtastic_DeviceState devicestate;
extern meshtastic_NodeDatabase nodeDatabase;
extern meshtastic_ChannelFile channelFile;
extern meshtastic_MyNodeInfo &myNodeInfo;
extern meshtastic_LocalConfig config;
extern meshtastic_DeviceUIConfig uiconfig;
extern meshtastic_LocalModuleConfig moduleConfig;
extern meshtastic_User &owner;
extern meshtastic_Position localPosition;

static constexpr const char *deviceStateFileName = "/prefs/device.proto";
static constexpr const char *legacyPrefFileName = "/prefs/db.proto";
static constexpr const char *nodeDatabaseFileName = "/prefs/nodes.proto";
static constexpr const char *configFileName = "/prefs/config.proto";
static constexpr const char *uiconfigFileName = "/prefs/uiconfig.proto";
static constexpr const char *moduleConfigFileName = "/prefs/module.proto";
static constexpr const char *channelFileName = "/prefs/channels.proto";
static constexpr const char *backupFileName = "/backups/backup.proto";

/// Given a node, return how many seconds in the past (vs now) that we last heard from it
uint32_t sinceLastSeen(const meshtastic_NodeInfoLite *n);

/// Given a packet, return how many seconds in the past (vs now) it was received
uint32_t sinceReceived(const meshtastic_MeshPacket *p);

enum LoadFileResult {
    // Successfully opened the file
    LOAD_SUCCESS = 1,
    // File does not exist
    NOT_FOUND = 2,
    // Device does not have a filesystem
    NO_FILESYSTEM = 3,
    // File exists, but could not decode protobufs
    DECODE_FAILED = 4,
    // File exists, but open failed for some reason
    OTHER_FAILURE = 5
};

enum UserLicenseStatus { NotKnown, NotLicensed, Licensed };

class NodeDB
{
    // NodeNum provisionalNodeNum; // if we are trying to find a node num this is our current attempt

    // A NodeInfo for every node we've seen
    // Eventually use a smarter datastructure
    // HashMap<NodeNum, NodeInfo> nodes;
    // Note: these two references just point into our static array we serialize to/from disk

  public:
    std::vector<meshtastic_NodeInfoLite> *meshNodes;
    bool updateGUI = false; // we think the gui should definitely be redrawn, screen will clear this once handled
    meshtastic_NodeInfoLite *updateGUIforNode = NULL; // if currently showing this node, we think you should update the GUI
    Observable<const meshtastic::NodeStatus *> newStatus;
    pb_size_t numMeshNodes;

    bool keyIsLowEntropy = false;
    bool hasWarned = false;

    /// don't do mesh based algorithm for node id assignment (initially)
    /// instead just store in flash - possibly even in the initial alpha release do this hack
    NodeDB();

    /// write to flash
    /// @return true if the save was successful
    bool saveToDisk(int saveWhat = SEGMENT_CONFIG | SEGMENT_MODULECONFIG | SEGMENT_DEVICESTATE | SEGMENT_CHANNELS |
                                   SEGMENT_NODEDATABASE);

    /** Reinit radio config if needed, because either:
     * a) sometimes a buggy android app might send us bogus settings or
     * b) the client set factory_reset
     *
     * @param factory_reset if true, reset all settings to factory defaults
     * @param is_fresh_install set to true after a fresh install, to trigger NodeInfo/Position requests
     * @return true if the config was completely reset, in that case, we should send it back to the client
     */
    void resetRadioConfig(bool is_fresh_install = false);

    /// given a subpacket sniffed from the network, update our DB state
    /// we updateGUI and updateGUIforNode if we think our this change is big enough for a redraw
    void updateFrom(const meshtastic_MeshPacket &p);

    void addFromContact(const meshtastic_SharedContact);

    /** Update position info for this node based on received position data
     */
    void updatePosition(uint32_t nodeId, const meshtastic_Position &p, RxSource src = RX_SRC_RADIO);

    /** Update telemetry info for this node based on received metrics
     */
    void updateTelemetry(uint32_t nodeId, const meshtastic_Telemetry &t, RxSource src = RX_SRC_RADIO);

    /** Update user info and channel for this node based on received user data
     */
    bool updateUser(uint32_t nodeId, meshtastic_User &p, uint8_t channelIndex = 0);

    /*
     * Sets a node either favorite or unfavorite
     */
    void set_favorite(bool is_favorite, uint32_t nodeId);

    /**
     * Other functions like the node picker can request a pause in the node sorting
     */
    void pause_sort(bool paused);

    /// @return our node number
    NodeNum getNodeNum() { return myNodeInfo.my_node_num; }

    // @return last byte of a NodeNum, 0xFF if it ended at 0x00
    uint8_t getLastByteOfNodeNum(NodeNum num) { return (uint8_t)((num & 0xFF) ? (num & 0xFF) : 0xFF); }

    /// if returns false, that means our node should send a DenyNodeNum response.  If true, we think the number is okay for use
    // bool handleWantNodeNum(NodeNum n);

    /* void handleDenyNodeNum(NodeNum FIXME read mesh proto docs, perhaps picking a random node num is not a great idea
    and instead we should use a special 'im unconfigured node number' and include our desired node number in the wantnum message.
    the unconfigured node num would only be used while initially joining the mesh so low odds of conflicting (especially if we
    randomly select from a small number of nodenums which can be used temporarily for this operation).  figure out what the lower
    level mesh sw does if it does conflict?  would it be better for people who are replying with denynode num to just broadcast
    their denial?)
    */

    // get channel channel index we heard a nodeNum on, defaults to 0 if not found
    uint8_t getMeshNodeChannel(NodeNum n);

    /* Return the number of nodes we've heard from recently (within the last 2 hrs?)
     * @param localOnly if true, ignore nodes heard via MQTT
     */
    size_t getNumOnlineMeshNodes(bool localOnly = false);

    void initConfigIntervals(), initModuleConfigIntervals(), resetNodes(), removeNodeByNum(NodeNum nodeNum);

    bool factoryReset(bool eraseBleBonds = false);

    LoadFileResult loadProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields,
                             void *dest_struct);
    bool saveProto(const char *filename, size_t protoSize, const pb_msgdesc_t *fields, const void *dest_struct,
                   bool fullAtomic = true);

    void installRoleDefaults(meshtastic_Config_DeviceConfig_Role role);

    const meshtastic_NodeInfoLite *readNextMeshNode(uint32_t &readIndex);

    meshtastic_NodeInfoLite *getMeshNodeByIndex(size_t x)
    {
        assert(x < numMeshNodes);
        return &meshNodes->at(x);
    }

    virtual meshtastic_NodeInfoLite *getMeshNode(NodeNum n);
    size_t getNumMeshNodes() { return numMeshNodes; }

    UserLicenseStatus getLicenseStatus(uint32_t nodeNum);

    size_t getMaxNodesAllocatedSize()
    {
        meshtastic_NodeDatabase emptyNodeDatabase;
        emptyNodeDatabase.version = DEVICESTATE_CUR_VER;
        size_t nodeDatabaseSize;
        pb_get_encoded_size(&nodeDatabaseSize, meshtastic_NodeDatabase_fields, &emptyNodeDatabase);
        return nodeDatabaseSize + (MAX_NUM_NODES * meshtastic_NodeInfoLite_size);
    }

    // returns true if the maximum number of nodes is reached or we are running low on memory
    bool isFull();

    void clearLocalPosition();

    void setLocalPosition(meshtastic_Position position, bool timeOnly = false)
    {
        if (timeOnly) {
            LOG_DEBUG("Set local position time only: time=%u timestamp=%u", position.time, position.timestamp);
            localPosition.time = position.time;
            localPosition.timestamp = position.timestamp > 0 ? position.timestamp : position.time;
            return;
        }
        LOG_DEBUG("Set local position: lat=%i lon=%i time=%u timestamp=%u", position.latitude_i, position.longitude_i,
                  position.time, position.timestamp);
        localPosition = position;
    }

    bool hasValidPosition(const meshtastic_NodeInfoLite *n);

    bool checkLowEntropyPublicKey(const meshtastic_Config_SecurityConfig_public_key_t &keyToTest);

    bool backupPreferences(meshtastic_AdminMessage_BackupLocation location);
    bool restorePreferences(meshtastic_AdminMessage_BackupLocation location,
                            int restoreWhat = SEGMENT_CONFIG | SEGMENT_MODULECONFIG | SEGMENT_DEVICESTATE | SEGMENT_CHANNELS);

    /// Notify observers of changes to the DB
    void notifyObservers(bool forceUpdate = false)
    {
        // Notify observers of the current node state
        const meshtastic::NodeStatus status = meshtastic::NodeStatus(getNumOnlineMeshNodes(), getNumMeshNodes(), forceUpdate);
        newStatus.notifyObservers(&status);
    }

  private:
    bool duplicateWarned = false;
    uint32_t lastNodeDbSave = 0;    // when we last saved our db to flash
    uint32_t lastBackupAttempt = 0; // when we last tried a backup automatically or manually
    uint32_t lastSort = 0;          // When last sorted the nodeDB
    /// Find a node in our DB, create an empty NodeInfoLite if missing
    meshtastic_NodeInfoLite *getOrCreateMeshNode(NodeNum n);

    /*
     * Internal boolean to track sorting paused
     */
    bool sortingIsPaused = false;

    /// pick a provisional nodenum we hope no one is using
    void pickNewNodeNum();

    /// read our db from flash
    void loadFromDisk();

    /// purge db entries without user info
    void cleanupMeshDB();

    /// Reinit device state from scratch (not loading from disk)
    void installDefaultDeviceState(), installDefaultNodeDatabase(), installDefaultChannels(),
        installDefaultConfig(bool preserveKey), installDefaultModuleConfig();

    /// write to flash
    /// @return true if the save was successful
    bool saveToDiskNoRetry(int saveWhat);

    bool saveChannelsToDisk();
    bool saveDeviceStateToDisk();
    bool saveNodeDatabaseToDisk();
    void sortMeshDB();
};

extern NodeDB *nodeDB;

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

/** The current change # for radio settings.  Starts at 0 on boot and any time the radio settings
 * might have changed is incremented.  Allows others to detect they might now be on a new channel.
 */
extern uint32_t radioGeneration;

extern meshtastic_CriticalErrorCode error_code;

/*
 * A numeric error address (nonzero if available)
 */
extern uint32_t error_address;
#define NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_SHIFT 0
#define NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK (1 << NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_SHIFT)

#define Module_Config_size                                                                                                       \
    (ModuleConfig_CannedMessageConfig_size + ModuleConfig_ExternalNotificationConfig_size + ModuleConfig_MQTTConfig_size +       \
     ModuleConfig_RangeTestConfig_size + ModuleConfig_SerialConfig_size + ModuleConfig_StoreForwardConfig_size +                 \
     ModuleConfig_TelemetryConfig_size + ModuleConfig_size)

// Please do not remove this comment, it makes trunk and compiler happy at the same time.