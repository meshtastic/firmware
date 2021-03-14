
#include <Arduino.h>
#include <assert.h>

#include "FS.h"

#include "Channels.h"
#include "CryptoEngine.h"
#include "FSCommon.h"
#include "GPS.h"
#include "MeshRadio.h"
#include "NodeDB.h"
#include "PacketHistory.h"
#include "PowerFSM.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "error.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include <pb_decode.h>
#include <pb_encode.h>

#ifndef NO_ESP32
#include "mesh/http/WiFiAPClient.h"
#endif

NodeDB nodeDB;

// we have plenty of ram so statically alloc this tempbuf (for now)
EXT_RAM_ATTR DeviceState devicestate;
MyNodeInfo &myNodeInfo = devicestate.my_node;
RadioConfig radioConfig;
ChannelFile channelFile;

/** The current change # for radio settings.  Starts at 0 on boot and any time the radio settings
 * might have changed is incremented.  Allows others to detect they might now be on a new channel.
 */
uint32_t radioGeneration;

/*
DeviceState versions used to be defined in the .proto file but really only this function cares.  So changed to a
#define here.
*/

#define DEVICESTATE_CUR_VER 11
#define DEVICESTATE_MIN_VER DEVICESTATE_CUR_VER

// FIXME - move this somewhere else
extern void getMacAddr(uint8_t *dmac);

/**
 *
 * Normally userids are unique and start with +country code to look like Signal phone numbers.
 * But there are some special ids used when we haven't yet been configured by a user.  In that case
 * we use !macaddr (no colons).
 */
User &owner = devicestate.owner;

static uint8_t ourMacAddr[6];

/**
 * The node number the user is currently looking at
 * 0 if none
 */
NodeNum displayedNodeNum;

NodeDB::NodeDB() : nodes(devicestate.node_db), numNodes(&devicestate.node_db_count) {}

/**
 * Most (but not always) of the time we want to treat packets 'from' the local phone (where from == 0), as if they originated on
 * the local node. If from is zero this function returns our node number instead
 */
NodeNum getFrom(const MeshPacket *p)
{
    return (p->from == 0) ? nodeDB.getNodeNum() : p->from;
}

bool NodeDB::resetRadioConfig()
{
    bool didFactoryReset = false;

    radioGeneration++;

    if (radioConfig.preferences.factory_reset) {
        DEBUG_MSG("Performing factory reset!\n");
        installDefaultDeviceState();
        didFactoryReset = true;
    } else if (channelFile.channels_count == 0) {
        DEBUG_MSG("Setting default channel and radio preferences!\n");

        channels.initDefaults();

        radioConfig.has_preferences = true;
    }

    channels.onConfigChanged();

    // temp hack for quicker testing
    // devicestate.no_save = true;
    if (devicestate.no_save) {
        DEBUG_MSG("***** DEVELOPMENT MODE - DO NOT RELEASE *****\n");

        // Sleep quite frequently to stress test the BLE comms, broadcast position every 6 mins
        radioConfig.preferences.screen_on_secs = 10;
        radioConfig.preferences.wait_bluetooth_secs = 10;
        radioConfig.preferences.position_broadcast_secs = 6 * 60;
        radioConfig.preferences.ls_secs = 60;
        radioConfig.preferences.region = RegionCode_TW;

        // Enter super deep sleep soon and stay there not very long
        // radioConfig.preferences.mesh_sds_timeout_secs = 10;
        // radioConfig.preferences.sds_secs = 60;
    }

    // Update the global myRegion
    initRegion();

    return didFactoryReset;
}

void NodeDB::installDefaultRadioConfig()
{
    memset(&radioConfig, 0, sizeof(radioConfig));
    radioConfig.has_preferences = true;
    resetRadioConfig();
}

void NodeDB::installDefaultChannels()
{
    memset(&channelFile, 0, sizeof(channelFile));
}

void NodeDB::installDefaultDeviceState()
{
    // We try to preserve the region setting because it will really bum users out if we discard it
    String oldRegion = myNodeInfo.region;
    RegionCode oldRegionCode = radioConfig.preferences.region;

    memset(&devicestate, 0, sizeof(devicestate));

    *numNodes = 0; // Forget node DB

    // init our devicestate with valid flags so protobuf writing/reading will work
    devicestate.has_my_node = true;
    devicestate.has_owner = true;
    devicestate.node_db_count = 0;
    devicestate.version = DEVICESTATE_CUR_VER;
    devicestate.receive_queue_count = 0; // Not yet implemented FIXME

    // default to no GPS, until one has been found by probing
    myNodeInfo.has_gps = false;
    myNodeInfo.message_timeout_msec = FLOOD_EXPIRE_TIME;
    generatePacketId(); // FIXME - ugly way to init current_packet_id;

    // Init our blank owner info to reasonable defaults
    getMacAddr(ourMacAddr);

    // Set default owner name
    pickNewNodeNum(); // based on macaddr now
    sprintf(owner.long_name, "Unknown %02x%02x", ourMacAddr[4], ourMacAddr[5]);
    sprintf(owner.short_name, "?%02X", (unsigned)(myNodeInfo.my_node_num & 0xff));

    sprintf(owner.id, "!%08x", getNodeNum()); // Default node ID now based on nodenum
    memcpy(owner.macaddr, ourMacAddr, sizeof(owner.macaddr));

    // Restore region if possible
    if (oldRegionCode != RegionCode_Unset)
        radioConfig.preferences.region = oldRegionCode;
    if (oldRegion.length()) // If the old style region was set, try to keep it up-to-date
        strcpy(myNodeInfo.region, oldRegion.c_str());

    installDefaultChannels();
    installDefaultRadioConfig();
}

void NodeDB::init()
{
    installDefaultDeviceState();

    // saveToDisk();
    loadFromDisk();
    // saveToDisk();

    myNodeInfo.max_channels = MAX_NUM_CHANNELS; // tell others the max # of channels we can understand

    myNodeInfo.error_code =
        CriticalErrorCode_None; // For the error code, only show values from this boot (discard value from flash)
    myNodeInfo.error_address = 0;

    // likewise - we always want the app requirements to come from the running appload
    myNodeInfo.min_app_version = 20200; // format is Mmmss (where M is 1+the numeric major number. i.e. 20120 means 1.1.20

    // Note! We do this after loading saved settings, so that if somehow an invalid nodenum was stored in preferences we won't
    // keep using that nodenum forever. Crummy guess at our nodenum (but we will check against the nodedb to avoid conflicts)
    pickNewNodeNum();

    // Include our owner in the node db under our nodenum
    NodeInfo *info = getOrCreateNode(getNodeNum());
    info->user = owner;
    info->has_user = true;
    info->hw_model = HW_VENDOR;

    // removed from 1.2 (though we do use old values if found)
    // We set these _after_ loading from disk - because they come from the build and are more trusted than
    // what is stored in flash
    //if (xstr(HW_VERSION)[0])
    //    strncpy(myNodeInfo.region, optstr(HW_VERSION), sizeof(myNodeInfo.region));
    // else DEBUG_MSG("This build does not specify a HW_VERSION\n"); // Eventually new builds will no longer include this build flag

    // DEBUG_MSG("legacy region %d\n", devicestate.legacyRadio.preferences.region);
    if(radioConfig.preferences.region == RegionCode_Unset)
        radioConfig.preferences.region = devicestate.legacyRadio.preferences.region;

    // Check for the old style of region code strings, if found, convert to the new enum.
    // Those strings will look like "1.0-EU433"
    if (radioConfig.preferences.region == RegionCode_Unset && strncmp(myNodeInfo.region, "1.0-", 4) == 0) {
        const char *regionStr = myNodeInfo.region + 4; // EU433 or whatever
        for (const RegionInfo *r = regions; r->code != RegionCode_Unset; r++)
            if (strcmp(r->name, regionStr) == 0) {
                radioConfig.preferences.region = r->code;
                break;
            }
    }

    strncpy(myNodeInfo.firmware_version, optstr(APP_VERSION), sizeof(myNodeInfo.firmware_version));
    
    // hw_model is no longer stored in myNodeInfo (as of 1.2.11) - we now store it as an enum in nodeinfo
    myNodeInfo.hw_model[0] = '\0';
    // strncpy(myNodeInfo.hw_model, HW_VENDOR, sizeof(myNodeInfo.hw_model));

    resetRadioConfig(); // If bogus settings got saved, then fix them

    DEBUG_MSG("region=%d, NODENUM=0x%x, dbsize=%d\n", radioConfig.preferences.region, myNodeInfo.my_node_num, *numNodes);
}

// We reserve a few nodenums for future use
#define NUM_RESERVED 4

/**
 * get our starting (provisional) nodenum from flash.
 */
void NodeDB::pickNewNodeNum()
{
    NodeNum r = myNodeInfo.my_node_num;

    // If we don't have a nodenum at app - pick an initial nodenum based on the macaddr
    if (r == 0)
        r = (ourMacAddr[2] << 24) | (ourMacAddr[3] << 16) | (ourMacAddr[4] << 8) | ourMacAddr[5];

    if (r == NODENUM_BROADCAST || r < NUM_RESERVED)
        r = NUM_RESERVED; // don't pick a reserved node number

    NodeInfo *found;
    while ((found = getNode(r)) && memcmp(found->user.macaddr, owner.macaddr, sizeof(owner.macaddr))) {
        NodeNum n = random(NUM_RESERVED, NODENUM_BROADCAST); // try a new random choice
        DEBUG_MSG("NOTE! Our desired nodenum 0x%x is in use, so trying for 0x%x\n", r, n);
        r = n;
    }

    myNodeInfo.my_node_num = r;
}

static const char *preffileOld = "/db.proto";
static const char *preffile = "/prefs/db.proto";
static const char *radiofile = "/prefs/radio.proto";
static const char *channelfile = "/prefs/channels.proto";
// const char *preftmp = "/db.proto.tmp";

/** Load a protobuf from a file, return true for success */
bool loadProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields, void *dest_struct)
{
#ifdef FS
    // static DeviceState scratch; We no longer read into a tempbuf because this structure is 15KB of valuable RAM

    auto f = FS.open(filename);

    // FIXME, temporary hack until every node in the universe is 1.2 or later - look for prefs in the old location (so we can
    // preserve region)
    if (!f && filename == preffile) {
        filename = preffileOld;
        f = FS.open(filename);
    }

    bool okay = false;
    if (f) {
        DEBUG_MSG("Loading %s\n", filename);
        pb_istream_t stream = {&readcb, &f, protoSize};

        // DEBUG_MSG("Preload channel name=%s\n", channelSettings.name);

        memset(dest_struct, 0, objSize);
        if (!pb_decode(&stream, fields, dest_struct)) {
            DEBUG_MSG("Error: can't decode protobuf %s\n", PB_GET_ERROR(&stream));
        } else {
            okay = true;
        }

        f.close();
    } else {
        DEBUG_MSG("No %s preferences found\n", filename);
    }
#else
    DEBUG_MSG("ERROR: Filesystem not implemented\n");
#endif
    return okay;
}

void NodeDB::loadFromDisk()
{
    // static DeviceState scratch; We no longer read into a tempbuf because this structure is 15KB of valuable RAM
    if (!loadProto(preffile, DeviceState_size, sizeof(devicestate), DeviceState_fields, &devicestate)) {
        installDefaultDeviceState(); // Our in RAM copy might now be corrupt
    } else {
        if (devicestate.version < DEVICESTATE_MIN_VER) {
            DEBUG_MSG("Warn: devicestate %d is old, discarding\n", devicestate.version);
            installDefaultDeviceState();
        } else {
            DEBUG_MSG("Loaded saved preferences version %d\n", devicestate.version);
        }
    }

    if (!loadProto(radiofile, RadioConfig_size, sizeof(RadioConfig), RadioConfig_fields, &radioConfig)) {
        installDefaultRadioConfig(); // Our in RAM copy might now be corrupt
    }

    if (!loadProto(channelfile, ChannelFile_size, sizeof(ChannelFile), ChannelFile_fields, &channelFile)) {
        installDefaultChannels(); // Our in RAM copy might now be corrupt
    }
}

/** Save a protobuf from a file, return true for success */
bool saveProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields, const void *dest_struct)
{
#ifdef FS
    // static DeviceState scratch; We no longer read into a tempbuf because this structure is 15KB of valuable RAM
    String filenameTmp = filename;
    filenameTmp += ".tmp";
    auto f = FS.open(filenameTmp.c_str(), FILE_O_WRITE);
    bool okay = false;
    if (f) {
        DEBUG_MSG("Saving %s\n", filename);
        pb_ostream_t stream = {&writecb, &f, protoSize};

        if (!pb_encode(&stream, fields, dest_struct)) {
            DEBUG_MSG("Error: can't encode protobuf %s\n", PB_GET_ERROR(&stream));
        } else {
            okay = true;
        }

        f.close();

        // brief window of risk here ;-)
        if (!FS.remove(filename))
            DEBUG_MSG("Warning: Can't remove old pref file\n");
        if (!FS.rename(filenameTmp.c_str(), filename))
            DEBUG_MSG("Error: can't rename new pref file\n");
    } else {
        DEBUG_MSG("Can't write prefs\n");
    }
#else
    DEBUG_MSG("ERROR: Filesystem not implemented\n");
#endif
    return okay;
}

void NodeDB::saveChannelsToDisk()
{
    if (!devicestate.no_save) {
#ifdef FS
        FS.mkdir("/prefs");
#endif
        saveProto(channelfile, ChannelFile_size, sizeof(ChannelFile), ChannelFile_fields, &channelFile);
    }  
}

void NodeDB::saveToDisk()
{
    if (!devicestate.no_save) {
#ifdef FS
        FS.mkdir("/prefs");
#endif
        bool okay = saveProto(preffile, DeviceState_size, sizeof(devicestate), DeviceState_fields, &devicestate);
        okay &= saveProto(radiofile, RadioConfig_size, sizeof(RadioConfig), RadioConfig_fields, &radioConfig);
        saveChannelsToDisk();

        // remove any pre 1.2 pref files, turn on after 1.2 is in beta
        // if(okay) FS.remove(preffileOld);
    } else {
        DEBUG_MSG("***** DEVELOPMENT MODE - DO NOT RELEASE - not saving to flash *****\n");
    }
}

const NodeInfo *NodeDB::readNextInfo()
{
    if (readPointer < *numNodes)
        return &nodes[readPointer++];
    else
        return NULL;
}

/// Given a node, return how many seconds in the past (vs now) that we last heard from it
uint32_t sinceLastSeen(const NodeInfo *n)
{
    uint32_t now = getTime();

    uint32_t last_seen = n->position.time;
    int delta = (int)(now - last_seen);
    if (delta < 0) // our clock must be slightly off still - not set from GPS yet
        delta = 0;

    return delta;
}

#define NUM_ONLINE_SECS (60 * 2) // 2 hrs to consider someone offline

size_t NodeDB::getNumOnlineNodes()
{
    size_t numseen = 0;

    // FIXME this implementation is kinda expensive
    for (int i = 0; i < *numNodes; i++)
        if (sinceLastSeen(&nodes[i]) < NUM_ONLINE_SECS)
            numseen++;

    return numseen;
}

#include "MeshPlugin.h"

/** Update position info for this node based on received position data
 */
void NodeDB::updatePosition(uint32_t nodeId, const Position &p)
{
    NodeInfo *info = getOrCreateNode(nodeId);

    DEBUG_MSG("DB update position node=0x%x time=%u, latI=%d, lonI=%d\n", nodeId, p.time, p.latitude_i, p.longitude_i);

    // Be careful to only update fields that have been set by the sender
    // A lot of position reports don't have time populated.  In that case, be careful to not blow away the time we
    // recorded based on the packet rxTime
    if (!info->position.time && p.time)
        info->position.time = p.time;
    if (p.battery_level)
        info->position.battery_level = p.battery_level;
    if (p.latitude_i || p.longitude_i) {
        info->position.latitude_i = p.latitude_i;
        info->position.longitude_i = p.longitude_i;
    }
    info->has_position = true;
    updateGUIforNode = info;
    notifyObservers(true); // Force an update whether or not our node counts have changed
}

/** Update user info for this node based on received user data
 */
void NodeDB::updateUser(uint32_t nodeId, const User &p)
{
    NodeInfo *info = getOrCreateNode(nodeId);

    DEBUG_MSG("old user %s/%s/%s\n", info->user.id, info->user.long_name, info->user.short_name);

    bool changed = memcmp(&info->user, &p,
                          sizeof(info->user)); // Both of these blocks start as filled with zero so I think this is okay

    info->user = p;
    DEBUG_MSG("updating changed=%d user %s/%s/%s\n", changed, info->user.id, info->user.long_name, info->user.short_name);
    info->has_user = true;

    if (changed) {
        updateGUIforNode = info;
        powerFSM.trigger(EVENT_NODEDB_UPDATED);
        notifyObservers(true); // Force an update whether or not our node counts have changed

        // Not really needed - we will save anyways when we go to sleep
        // We just changed something important about the user, store our DB
        // saveToDisk();
    }
}

/// given a subpacket sniffed from the network, update our DB state
/// we updateGUI and updateGUIforNode if we think our this change is big enough for a redraw
void NodeDB::updateFrom(const MeshPacket &mp)
{
    if (mp.which_payloadVariant == MeshPacket_decoded_tag) {
        DEBUG_MSG("Update DB node 0x%x, rx_time=%u\n", mp.from, mp.rx_time);

        NodeInfo *info = getOrCreateNode(getFrom(&mp));

        if (mp.rx_time) {              // if the packet has a valid timestamp use it to update our last_seen
            info->has_position = true; // at least the time is valid
            info->position.time = mp.rx_time;
        }

        info->snr = mp.rx_snr; // keep the most recent SNR we received for this node.
    }
}

/// Find a node in our DB, return null for missing
/// NOTE: This function might be called from an ISR
NodeInfo *NodeDB::getNode(NodeNum n)
{
    for (int i = 0; i < *numNodes; i++)
        if (nodes[i].num == n)
            return &nodes[i];

    return NULL;
}

/// Find a node in our DB, create an empty NodeInfo if missing
NodeInfo *NodeDB::getOrCreateNode(NodeNum n)
{
    NodeInfo *info = getNode(n);

    if (!info) {
        // add the node
        assert(*numNodes < MAX_NUM_NODES);
        info = &nodes[(*numNodes)++];

        // everything is missing except the nodenum
        memset(info, 0, sizeof(*info));
        info->num = n;
    }

    return info;
}

/// Record an error that should be reported via analytics
void recordCriticalError(CriticalErrorCode code, uint32_t address)
{
    // Print error to screen and serial port
    String lcd = String("Critical error ") + code + "!\n";
    screen->print(lcd.c_str());
    DEBUG_MSG("NOTE! Recording critical error %d, address=%lx\n", code, address);

    // Record error to DB
    myNodeInfo.error_code = code;
    myNodeInfo.error_address = address;
    myNodeInfo.error_count++;
}
