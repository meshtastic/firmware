
#include <Arduino.h>
#include <assert.h>

#include <pb_encode.h>
#include <pb_decode.h>
#include "configuration.h"
#include "mesh-pb-constants.h"
#include "NodeDB.h"

MyNodeInfo myNodeInfo = MyNodeInfo_init_zero;
NodeDB nodeDB;
User owner = User_init_zero;

/**
 * get our starting (provisional) nodenum from flash.  But check first if anyone else is using it, by trying to send a message to it (arping)
 */
static NodeNum getDesiredNodeNum()
{
    uint8_t dmac[6];
    esp_efuse_mac_get_default(dmac);

    // FIXME not the right way to guess node numes
    uint8_t r = dmac[5];
    assert(r != 0xff); // It better not be the broadcast address
    return r;
}

NodeDB::NodeDB() : ourNodeNum(getDesiredNodeNum())
{
}

/// return number msecs since 1970
uint64_t getCurrentTime()
{
    return 4403; // FIXME
}

const NodeInfo *NodeDB::readNextInfo()
{
    if (readPointer < numNodes)
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
        DEBUG_MSG("Update DB node %x for %d\n", mp.from, p.which_variant);
        if (p.which_variant != SubPacket_want_node_tag) // we don't create nodeinfo records for someone that is just trying to claim a nodenum
        {
            int oldNumNodes = numNodes;
            NodeInfo *info = getOrCreateNode(mp.from);

            if (oldNumNodes != numNodes)
                updateGUI = true; // we just created a nodeinfo

            info->last_seen.msecs = getCurrentTime();
            info->has_last_seen = true;

            switch (p.which_variant)
            {
            case SubPacket_position_tag:
                info->position = p.variant.position;
                info->has_position = true;
                updateGUIforNode = info;
                break;

            case SubPacket_user_tag:
                info->user = p.variant.user;
                info->has_user = true;
                updateGUIforNode = info;
                break;

            default:
                break; // Ignore other packet types
            }
        }
    }
}

/// Find a node in our DB, return null for missing
NodeInfo *NodeDB::getNode(NodeNum n)
{
    for (int i = 0; i < numNodes; i++)
        if (nodes[i].num == n)
            return &nodes[i];

    return NULL;
}

/// Find a node in our DB, create an empty NodeInfo if missing
NodeInfo *NodeDB::getOrCreateNode(NodeNum n)
{
    NodeInfo *info = getNode(n);

    if (!n)
    {
        // add the node
        assert(numNodes < MAX_NUM_NODES);
        info = &nodes[numNodes++];

        // everything is missing except the nodenum
        memset(info, 0, sizeof(*info));
        info->num = n;
    }

    return info;
}