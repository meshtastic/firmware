#include "NodeInfoModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

NodeInfoModule *nodeInfoModule;

bool NodeInfoModule::handleReceivedProtobuf(const MeshPacket &mp, User *pptr)
{
    auto p = *pptr;

    nodeDB.updateUser(getFrom(&mp), p);

    bool wasBroadcast = mp.to == NODENUM_BROADCAST;

    // Show new nodes on LCD screen
    if (wasBroadcast) {
        String lcd = String("Joined: ") + p.long_name + "\n";
        if (screen)
            screen->print(lcd.c_str());
    }

    // DEBUG_MSG("did handleReceived\n");
    return false; // Let others look at this message also if they want
}

void NodeInfoModule::sendOurNodeInfo(NodeNum dest, bool wantReplies)
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

MeshPacket *NodeInfoModule::allocReply()
{
    User &u = owner;

    DEBUG_MSG("sending owner %s/%s/%s\n", u.id, u.long_name, u.short_name);
    return allocDataProtobuf(u);
}

NodeInfoModule::NodeInfoModule()
    : ProtobufModule("nodeinfo", PortNum_NODEINFO_APP, User_fields), concurrency::OSThread("NodeInfoModule")
{
    isPromiscuous = true; // We always want to update our nodedb, even if we are sniffing on others
    setIntervalFromNow(30 * 1000); // Send our initial owner announcement 30 seconds after we start (to give network time to setup)
}

int32_t NodeInfoModule::runOnce()
{
    static uint32_t currentGeneration;

    // If we changed channels, ask everyone else for their latest info
    bool requestReplies = currentGeneration != radioGeneration;
    currentGeneration = radioGeneration;

    DEBUG_MSG("Sending our nodeinfo to mesh (wantReplies=%d)\n", requestReplies);
    sendOurNodeInfo(NODENUM_BROADCAST, requestReplies); // Send our info (don't request replies)

    return default_broadcast_interval_secs * 1000;
}