
#include <Arduino.h>
#include <assert.h>

#include "FS.h"
#include "SPIFFS.h"

#include "GPS.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "error.h"
#include "mesh-pb-constants.h"
#include <pb_decode.h>
#include <pb_encode.h>

NodeDB nodeDB;

// we have plenty of ram so statically alloc this tempbuf (for now)
DeviceState devicestate;
MyNodeInfo &myNodeInfo = devicestate.my_node;
RadioConfig &radioConfig = devicestate.radio;
ChannelSettings &channelSettings = radioConfig.channel_settings;

/*
DeviceState versions used to be defined in the .proto file but really only this function cares.  So changed to a
#define here.
*/

#define DEVICESTATE_CUR_VER 7
#define DEVICESTATE_MIN_VER DEVICESTATE_CUR_VER

#ifndef NO_ESP32
#define FS SPIFFS
#endif

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

NodeDB::NodeDB() : nodes(devicestate.node_db), numNodes(&devicestate.node_db_count) {}

void NodeDB::resetRadioConfig()
{
    /// 16 bytes of random PSK for our _public_ default channel that all devices power up on
    static const uint8_t defaultpsk[] = {0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
                                         0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0xbf};

    if (radioConfig.preferences.sds_secs == 0) {
        DEBUG_MSG("RadioConfig reset!\n");
        radioConfig.preferences.send_owner_interval = 4; // per sw-design.md
        radioConfig.preferences.position_broadcast_secs = 15 * 60;
        radioConfig.preferences.wait_bluetooth_secs = 120;
        radioConfig.preferences.screen_on_secs = 5 * 60;
        radioConfig.preferences.mesh_sds_timeout_secs = 2 * 60 * 60;
        radioConfig.preferences.phone_sds_timeout_sec = 2 * 60 * 60;
        radioConfig.preferences.sds_secs = 365 * 24 * 60 * 60; // one year
        radioConfig.preferences.ls_secs = 60 * 60;
        radioConfig.preferences.phone_timeout_secs = 15 * 60;
        radioConfig.has_channel_settings = true;
        radioConfig.has_preferences = true;

        // radioConfig.modem_config = RadioConfig_ModemConfig_Bw125Cr45Sf128;  // medium range and fast
        // channelSettings.modem_config = ChannelSettings_ModemConfig_Bw500Cr45Sf128;  // short range and fast, but wide bandwidth
        // so incompatible radios can talk together
        channelSettings.modem_config = ChannelSettings_ModemConfig_Bw125Cr48Sf4096; // slow and long range

        channelSettings.tx_power = 23;
        memcpy(&channelSettings.psk, &defaultpsk, sizeof(channelSettings.psk));
        strcpy(channelSettings.name, "Default");
    }

    // temp hack for quicker testing
    /*
    radioConfig.preferences.screen_on_secs = 30;
    radioConfig.preferences.wait_bluetooth_secs = 30;
    radioConfig.preferences.position_broadcast_secs = 15;
    */
}

void NodeDB::init()
{
    // init our devicestate with valid flags so protobuf writing/reading will work
    devicestate.has_my_node = true;
    devicestate.has_radio = true;
    devicestate.has_owner = true;
    devicestate.has_radio = false;
    devicestate.radio.has_channel_settings = true;
    devicestate.radio.has_preferences = true;
    devicestate.node_db_count = 0;
    devicestate.receive_queue_count = 0;

    resetRadioConfig();

    // default to no GPS, until one has been found by probing
    myNodeInfo.has_gps = false;

    strncpy(myNodeInfo.region, xstr(HW_VERSION), sizeof(myNodeInfo.region));
    strncpy(myNodeInfo.firmware_version, xstr(APP_VERSION), sizeof(myNodeInfo.firmware_version));
    strncpy(myNodeInfo.hw_model, HW_VENDOR, sizeof(myNodeInfo.hw_model));

    // Init our blank owner info to reasonable defaults
    getMacAddr(ourMacAddr);
    sprintf(owner.id, "!%02x%02x%02x%02x%02x%02x", ourMacAddr[0], ourMacAddr[1], ourMacAddr[2], ourMacAddr[3], ourMacAddr[4],
            ourMacAddr[5]);
    memcpy(owner.macaddr, ourMacAddr, sizeof(owner.macaddr));

    // make each node start with ad different random seed (but okay that the sequence is the same each boot)
    randomSeed((ourMacAddr[2] << 24L) | (ourMacAddr[3] << 16L) | (ourMacAddr[4] << 8L) | ourMacAddr[5]);

    sprintf(owner.long_name, "Unknown %02x%02x", ourMacAddr[4], ourMacAddr[5]);
    sprintf(owner.short_name, "?%02X", ourMacAddr[5]);

    // Crummy guess at our nodenum
    pickNewNodeNum();

    // Include our owner in the node db under our nodenum
    NodeInfo *info = getOrCreateNode(getNodeNum());
    info->user = owner;
    info->has_user = true;

    // saveToDisk();
    loadFromDisk();
    resetRadioConfig(); // If bogus settings got saved, then fix them

    DEBUG_MSG("NODENUM=0x%x, dbsize=%d\n", myNodeInfo.my_node_num, *numNodes);
}

// We reserve a few nodenums for future use
#define NUM_RESERVED 4

/**
 * get our starting (provisional) nodenum from flash.
 */
void NodeDB::pickNewNodeNum()
{
    // FIXME not the right way to guess node numes
    uint8_t r = ourMacAddr[5];
    if (r == 0xff || r < NUM_RESERVED)
        r = NUM_RESERVED; // don't pick a reserved node number

    NodeInfo *found;
    while ((found = getNode(r)) && memcmp(found->user.macaddr, owner.macaddr, sizeof(owner.macaddr))) {
        NodeNum n = random(NUM_RESERVED, NODENUM_BROADCAST); // try a new random choice
        DEBUG_MSG("NOTE! Our desired nodenum 0x%x is in use, so trying for 0x%x\n", r, n);
        r = n;
    }

    myNodeInfo.my_node_num = r;
}

const char *preffile = "/db.proto";
const char *preftmp = "/db.proto.tmp";

void NodeDB::loadFromDisk()
{
#ifdef FS
    static DeviceState scratch;

    if (!FS.begin(true)) // FIXME - do this in main?
    {
        DEBUG_MSG("ERROR SPIFFS Mount Failed\n");
        // FIXME - report failure to phone
    }

    File f = FS.open(preffile);
    if (f) {
        DEBUG_MSG("Loading saved preferences\n");
        pb_istream_t stream = {&readcb, &f, DeviceState_size};

        // DEBUG_MSG("Preload channel name=%s\n", channelSettings.name);

        memset(&scratch, 0, sizeof(scratch));
        if (!pb_decode(&stream, DeviceState_fields, &scratch)) {
            DEBUG_MSG("Error: can't decode protobuf %s\n", PB_GET_ERROR(&stream));
            // FIXME - report failure to phone
        } else {
            if (scratch.version < DEVICESTATE_MIN_VER)
                DEBUG_MSG("Warn: devicestate is old, discarding\n");
            else {
                DEBUG_MSG("Loaded saved preferences version %d\n", scratch.version);
                devicestate = scratch;
            }

            // DEBUG_MSG("Postload channel name=%s\n", channelSettings.name);
        }

        f.close();
    } else {
        DEBUG_MSG("No saved preferences found\n");
    }
#else
    DEBUG_MSG("ERROR: Filesystem not implemented\n");
#endif
}

void NodeDB::saveToDisk()
{
#ifdef FS
    File f = FS.open(preftmp, "w");
    if (f) {
        DEBUG_MSG("Writing preferences\n");

        pb_ostream_t stream = {&writecb, &f, SIZE_MAX, 0};

        // DEBUG_MSG("Presave channel name=%s\n", channelSettings.name);

        devicestate.version = DEVICESTATE_CUR_VER;
        if (!pb_encode(&stream, DeviceState_fields, &devicestate)) {
            DEBUG_MSG("Error: can't write protobuf %s\n", PB_GET_ERROR(&stream));
            // FIXME - report failure to phone
        }

        f.close();

        // brief window of risk here ;-)
        if (!FS.remove(preffile))
            DEBUG_MSG("Warning: Can't remove old pref file\n");
        if (!FS.rename(preftmp, preffile))
            DEBUG_MSG("Error: can't rename new pref file\n");
    } else {
        DEBUG_MSG("ERROR: can't write prefs\n"); // FIXME report to app
    }
#else
    DEBUG_MSG("ERROR filesystem not implemented\n");
#endif
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
    uint32_t now = gps.getTime();

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

/// given a subpacket sniffed from the network, update our DB state
/// we updateGUI and updateGUIforNode if we think our this change is big enough for a redraw
void NodeDB::updateFrom(const MeshPacket &mp)
{
    if (mp.has_payload) {
        const SubPacket &p = mp.payload;
        DEBUG_MSG("Update DB node 0x%x, rx_time=%u\n", mp.from, mp.rx_time);

        int oldNumNodes = *numNodes;
        NodeInfo *info = getOrCreateNode(mp.from);

        if (oldNumNodes != *numNodes)
            updateGUI = true; // we just created a nodeinfo

        if (mp.rx_time) {              // if the packet has a valid timestamp use it to update our last_seen
            info->has_position = true; // at least the time is valid
            info->position.time = mp.rx_time;
        }

        info->snr = mp.rx_snr; // keep the most recent SNR we received for this node.

        if (p.has_position) {
            // we carefully preserve the old time, because we always trust our local timestamps more
            uint32_t oldtime = info->position.time;
            info->position = p.position;
            info->position.time = oldtime;
            info->has_position = true;
            updateGUIforNode = info;
        }

        if (p.has_data) {
            // Keep a copy of the most recent text message.
            if (p.data.typ == Data_Type_CLEAR_TEXT) {
                DEBUG_MSG("Received text msg from=0%0x, msg=%.*s\n", mp.from, p.data.payload.size, p.data.payload.bytes);
                if (mp.to == NODENUM_BROADCAST || mp.to == nodeDB.getNodeNum()) {
                    // We only store/display messages destined for us.
                    devicestate.rx_text_message = mp;
                    devicestate.has_rx_text_message = true;
                    updateTextMessage = true;
                    powerFSM.trigger(EVENT_RECEIVED_TEXT_MSG);
                }
            }
        }

        if (p.has_user) {
            DEBUG_MSG("old user %s/%s/%s\n", info->user.id, info->user.long_name, info->user.short_name);

            bool changed = memcmp(&info->user, &p.user,
                                  sizeof(info->user)); // Both of these blocks start as filled with zero so I think this is okay

            info->user = p.user;
            DEBUG_MSG("updating changed=%d user %s/%s/%s\n", changed, info->user.id, info->user.long_name, info->user.short_name);
            info->has_user = true;

            if (changed) {
                updateGUIforNode = info;
                powerFSM.trigger(EVENT_NODEDB_UPDATED);

                // Not really needed - we will save anyways when we go to sleep
                // We just changed something important about the user, store our DB
                // saveToDisk();
            }
        }
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
    DEBUG_MSG("NOTE! Recording critical error %d, address=%x\n", code, address);
    myNodeInfo.error_code = code;
    myNodeInfo.error_address = address;
    myNodeInfo.error_count++;
}
