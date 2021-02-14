#include "NodeInfoPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

NodeInfoPlugin *nodeInfoPlugin;

bool NodeInfoPlugin::handleReceivedProtobuf(const MeshPacket &mp, const User &p)
{
    // FIXME - we currently update NodeInfo data in the DB only if the message was a broadcast or destined to us
    // it would be better to update even if the message was destined to others.

    nodeDB.updateUser(mp.from, p);

    bool wasBroadcast = mp.to == NODENUM_BROADCAST;

    // Show new nodes on LCD screen
    if (wasBroadcast) {
        String lcd = String("Joined: ") + p.long_name + "\n";
        screen->print(lcd.c_str());
    }

    return false; // Let others look at this message also if they want
}

void NodeInfoPlugin::sendOurNodeInfo(NodeNum dest, bool wantReplies)
{
    // cancel any not yet sent (now stale) position packets
    if (prevPacketId) // if we wrap around to zero, we'll simply fail to cancel in that rare case (no big deal)
        service.cancelSending(prevPacketId);

    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;
    p->priority = MeshPacket_Priority_BACKGROUND;
    prevPacketId = p->id;

    service.sendToMesh(p);
}

MeshPacket *NodeInfoPlugin::allocReply()
{
    User &u = owner;

    DEBUG_MSG("sending owner %s/%s/%s\n", u.id, u.long_name, u.short_name);
    return allocDataProtobuf(u);
}

NodeInfoPlugin::NodeInfoPlugin()
    : ProtobufPlugin("nodeinfo", PortNum_NODEINFO_APP, User_fields), concurrency::OSThread("NodeInfoPlugin")
{
    setIntervalFromNow(30 *
                       1000); // Send our initial owner announcement 30 seconds after we start (to give network time to setup)
}

int32_t NodeInfoPlugin::runOnce()
{
    static uint32_t currentGeneration;

    // If we changed channels, ask everyone else for their latest info
    bool requestReplies = currentGeneration != radioGeneration;
    currentGeneration = radioGeneration;

    DEBUG_MSG("Sending our nodeinfo to mesh (wantReplies=%d)\n", requestReplies);
    assert(nodeInfoPlugin);
    nodeInfoPlugin->sendOurNodeInfo(NODENUM_BROADCAST, requestReplies); // Send our info (don't request replies)

    return getPref_position_broadcast_secs() * 1000;
}

