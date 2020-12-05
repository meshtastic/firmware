#include "NodeInfoPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

NodeInfoPlugin nodeInfoPlugin;

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
    User &u = owner;

    DEBUG_MSG("sending owner %s/%s/%s\n", u.id, u.long_name, u.short_name);
    MeshPacket *p = allocForSending(u);
    p->to = dest;
    p->decoded.want_response = wantReplies;

    service.sendToMesh(p);
}


/** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
 * so that subclasses can (optionally) send a response back to the original sender.  Implementing this method
 * is optional
 */
void NodeInfoPlugin::sendResponse(NodeNum to) {
    DEBUG_MSG("Sending user reply\n");
    sendOurNodeInfo(to, false);
}