
#include <Arduino.h>
#include <assert.h>

#include "FS.h"
#include "SPIFFS.h"

#include <pb_encode.h>
#include <pb_decode.h>
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "NodeDB.h"
#include "GPS.h"

NodeDB nodeDB;

// we have plenty of ram so statically alloc this tempbuf (for now)
DeviceState devicestate;
MyNodeInfo &myNodeInfo = devicestate.my_node;
RadioConfig &radioConfig = devicestate.radio;

#define FS SPIFFS

/** 
 * 
 * Normally userids are unique and start with +country code to look like Signal phone numbers.
 * But there are some special ids used when we haven't yet been configured by a user.  In that case
 * we use !macaddr (no colons).
 */
User &owner = devicestate.owner;

static uint8_t ourMacAddr[6];

NodeDB::NodeDB() : nodes(devicestate.node_db), numNodes(&devicestate.node_db_count)
{
}

void NodeDB::init()
{

    // init our devicestate with valid flags so protobuf writing/reading will work
    devicestate.has_my_node = true;
    devicestate.has_radio = true;
    devicestate.has_owner = true;
    devicestate.node_db_count = 0;
    devicestate.receive_queue_count = 0;

    // Init our blank owner info to reasonable defaults
    esp_efuse_mac_get_default(ourMacAddr);
    sprintf(owner.id, "!%02x%02x%02x%02x%02x%02x", ourMacAddr[0],
            ourMacAddr[1], ourMacAddr[2], ourMacAddr[3], ourMacAddr[4], ourMacAddr[5]);
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
    info->last_seen = 0; // haven't heard a real message yet

    if (!FS.begin(true)) // FIXME - do this in main?
    {
        DEBUG_MSG("ERROR SPIFFS Mount Failed\n");
        // FIXME - report failure to phone
    }

    // saveToDisk();
    loadFromDisk();

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
    while ((found = getNode(r)) && memcmp(found->user.macaddr, owner.macaddr, sizeof(owner.macaddr)))
    {
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
    static DeviceState scratch;

    File f = FS.open(preffile);
    if (f)
    {
        DEBUG_MSG("Loading saved preferences\n");
        pb_istream_t stream = {&readcb, &f, DeviceState_size};

        memset(&scratch, 0, sizeof(scratch));
        if (!pb_decode(&stream, DeviceState_fields, &scratch))
        {
            DEBUG_MSG("Error: can't decode protobuf %s\n", PB_GET_ERROR(&stream));
            // FIXME - report failure to phone
        }
        else
        {
            if (scratch.version < DeviceState_Version_Minimum)
                DEBUG_MSG("Warn: devicestate is old, discarding\n");
            else
                devicestate = scratch;
        }

        f.close();
    }
    else
    {
        DEBUG_MSG("No saved preferences found\n");
    }
}

void NodeDB::saveToDisk()
{
    File f = FS.open(preftmp, "w");
    if (f)
    {
        DEBUG_MSG("Writing preferences\n");
        pb_ostream_t stream = {&writecb, &f, DeviceState_size, 0};

        devicestate.version = DeviceState_Version_Current;
        if (!pb_encode(&stream, DeviceState_fields, &devicestate))
        {
            DEBUG_MSG("Error: can't write protobuf %s\n", PB_GET_ERROR(&stream));
            // FIXME - report failure to phone
        }

        f.close();

        // brief window of risk here ;-)
        FS.remove(preffile);
        FS.rename(preftmp, preffile);
    }
    else
    {
        DEBUG_MSG("ERROR: can't write prefs\n"); // FIXME report to app
    }
}

const NodeInfo *NodeDB::readNextInfo()
{
    if (readPointer < *numNodes)
        return &nodes[readPointer++];
    else
        return NULL;
}

/// given a subpacket sniffed from the network, update our DB state
/// we updateGUI and updateGUIforNode if we think our this change is big enough for a redraw
void NodeDB::updateFrom(const MeshPacket &mp)
{
    if (mp.has_payload)
    {
        const SubPacket &p = mp.payload;
        DEBUG_MSG("Update DB node 0x%x for variant %d\n", mp.from, p.which_variant);

        int oldNumNodes = *numNodes;
        NodeInfo *info = getOrCreateNode(mp.from);

        if (oldNumNodes != *numNodes)
            updateGUI = true; // we just created a nodeinfo

        info->last_seen = gps.getTime();

        switch (p.which_variant)
        {
        case SubPacket_position_tag:
            info->position = p.variant.position;
            info->has_position = true;
            updateGUIforNode = info;
            break;

        case SubPacket_user_tag:
        {
            DEBUG_MSG("old user %s/%s/%s\n", info->user.id, info->user.long_name, info->user.short_name);

            bool changed = memcmp(&info->user, &p.variant.user, sizeof(info->user)); // Both of these blocks start as filled with zero so I think this is okay

            info->user = p.variant.user;
            DEBUG_MSG("updating changed=%d user %s/%s/%s\n", changed, info->user.id, info->user.long_name, info->user.short_name);
            info->has_user = true;
            updateGUIforNode = info;

            if (changed)
            {
                // We just created a user for the first time, store our DB
                saveToDisk();
            }
            break;
        }

        default:
            break; // Ignore other packet types
        }
    }
}

/// Find a node in our DB, return null for missing
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

    if (!info)
    {
        // add the node
        assert(*numNodes < MAX_NUM_NODES);
        info = &nodes[(*numNodes)++];

        // everything is missing except the nodenum
        memset(info, 0, sizeof(*info));
        info->num = n;
    }

    return info;
}