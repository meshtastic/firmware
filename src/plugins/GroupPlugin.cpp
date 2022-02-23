#include "GroupPlugin.h"
#include "MeshService.h"
#include "Router.h"
#include "configuration.h"
#include "NodeDB.h"


GroupPlugin *groupPlugin;



GroupPlugin::GroupPlugin()
    : ProtobufPlugin("group", PortNum_GROUP_APP, GroupInfo_fields), concurrency::OSThread("GroupPlugin")
{

    strcpy(ourGroupInfo.group[0], "Avocado");
    strcpy(ourGroupInfo.group[1], "Backberries");
    strcpy(ourGroupInfo.group[2], "Cantaloupe");
    strcpy(ourGroupInfo.group[3], "Durian");
    strcpy(ourGroupInfo.group[4], "Elderberry");
    strcpy(ourGroupInfo.group[5], "Fig");
    strcpy(ourGroupInfo.group[6], "Guava");
    strcpy(ourGroupInfo.group[7], "Honeydew");
    strcpy(ourGroupInfo.group[8], "Jackfruit");
    strcpy(ourGroupInfo.group[9], "Kiwifruit");

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


    DEBUG_MSG("Group 0=%s\n", ourGroupInfo.group[0]);
    DEBUG_MSG("Group 1=%s\n", ourGroupInfo.group[1]);
    DEBUG_MSG("Group 2=%s\n", ourGroupInfo.group[2]);
    DEBUG_MSG("Group 3=%s\n", ourGroupInfo.group[3]);
    DEBUG_MSG("Group 4=%s\n", ourGroupInfo.group[4]);
    DEBUG_MSG("Group 5=%s\n", ourGroupInfo.group[5]);
    DEBUG_MSG("Group 6=%s\n", ourGroupInfo.group[6]);
    DEBUG_MSG("Group 7=%s\n", ourGroupInfo.group[7]);
    DEBUG_MSG("Group 8=%s\n", ourGroupInfo.group[8]);
    DEBUG_MSG("Group 9=%s\n", ourGroupInfo.group[9]);
    DEBUG_MSG("Group 10=%s\n", ourGroupInfo.group[10]);
    DEBUG_MSG("Group 11=%s\n", ourGroupInfo.group[11]);



    DEBUG_MSG("group plugin runOnce()\n");

    return 50000; // to save power only wake for our callback occasionally
}
