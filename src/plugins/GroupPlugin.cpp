#include "GroupPlugin.h"
#include "MeshService.h"
#include "Router.h"
#include "configuration.h"
#include "NodeDB.h"


GroupPlugin *groupPlugin;



GroupPlugin::GroupPlugin()
    : ProtobufPlugin("group", PortNum_GROUP_APP, GroupInfo_fields), concurrency::OSThread("GroupPlugin")
{
    //isPromiscuous = true;          // We always want to update our nodedb, even if we are sniffing on others
    //setIntervalFromNow(60 * 1000); // Send our initial position 60 seconds after we start (to give GPS time to setup)
}

bool GroupPlugin::handleReceivedProtobuf(const MeshPacket &mp, GroupInfo *pptr)
{
    //auto p = *pptr;  



    return false; // Let others look at this message also if they want
}

MeshPacket *GroupPlugin::allocReply()
{
    GroupInfo gi = GroupInfo_init_default; //   Start with an empty structure

    return allocDataProtobuf(gi);
}

int32_t GroupPlugin::runOnce()
{
    NodeInfo *node = nodeDB.getNode(nodeDB.getNodeNum());

    
    //ourGroupInfo.group[0] = "20";
    //ourGroupInfo.group_count;
    sprintf(ourGroupInfo.group[0], "Group 0");
    sprintf(ourGroupInfo.group[1], "Group 1");
    sprintf(ourGroupInfo.group[2], "Group 2");
    sprintf(ourGroupInfo.group[3], "Group 3");
    sprintf(ourGroupInfo.group[4], "Group 4");

    return 5000; // to save power only wake for our callback occasionally
}
