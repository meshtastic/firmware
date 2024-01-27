#include "NodeInfoModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

NodeInfoModule *nodeInfoModule;

bool NodeInfoModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_User *pptr)
{
    auto p = *pptr;

    bool hasChanged = nodeDB.updateUser(getFrom(&mp), p, mp.channel);

    bool wasBroadcast = mp.to == NODENUM_BROADCAST;

    // Show new nodes on LCD screen
    if (wasBroadcast) {
        String lcd = String("Joined: ") + p.long_name + "\n";
        if (screen)
            screen->print(lcd.c_str());
    }

    // if user has changed while packet was not for us, inform phone
    if (hasChanged && !wasBroadcast && mp.to != nodeDB.getNodeNum())
        service.sendToPhone(packetPool.allocCopy(mp));

    // LOG_DEBUG("did handleReceived\n");
    return false; // Let others look at this message also if they want
}

void NodeInfoModule::sendOurNodeInfo(NodeNum dest, bool wantReplies, uint8_t channel)
{
    // cancel any not yet sent (now stale) position packets
    if (prevPacketId) // if we wrap around to zero, we'll simply fail to cancel in that rare case (no big deal)
        service.cancelSending(prevPacketId);

    meshtastic_MeshPacket *p = allocReply();
    if (p) { // Check whether we didn't ignore it
        p->to = dest;
        p->decoded.want_response = (config.device.role != meshtastic_Config_DeviceConfig_Role_TRACKER &&
                                    config.device.role != meshtastic_Config_DeviceConfig_Role_SENSOR) &&
                                   wantReplies;
        p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        if (channel > 0) {
            LOG_DEBUG("sending ourNodeInfo to channel %d\n", channel);
            p->channel = channel;
        }

        prevPacketId = p->id;

        service.sendToMesh(p);
    }
}

meshtastic_MeshPacket *NodeInfoModule::allocReply()
{
    uint32_t now = millis();
    // If we sent our NodeInfo less than 1 min. ago, don't send it again as it may be still underway.
    if (lastSentToMesh && (now - lastSentToMesh) < 60 * 1000) {
        LOG_DEBUG("Sending NodeInfo will be ignored since we just sent it.\n");
        ignoreRequest = true; // Mark it as ignored for MeshModule
        return NULL;
    } else {
        ignoreRequest = false; // Don't ignore requests anymore
        meshtastic_User &u = owner;

        LOG_INFO("sending owner %s/%s/%s\n", u.id, u.long_name, u.short_name);
        lastSentToMesh = now;
        return allocDataProtobuf(u);
    }
}

NodeInfoModule::NodeInfoModule()
    : ProtobufModule("nodeinfo", meshtastic_PortNum_NODEINFO_APP, &meshtastic_User_msg), concurrency::OSThread("NodeInfoModule")
{
    isPromiscuous = true; // We always want to update our nodedb, even if we are sniffing on others
    setIntervalFromNow(30 *
                       1000); // Send our initial owner announcement 30 seconds after we start (to give network time to setup)
}

int32_t NodeInfoModule::runOnce()
{
    // If we changed channels, ask everyone else for their latest info
    bool requestReplies = currentGeneration != radioGeneration;
    currentGeneration = radioGeneration;

    if (airTime->isTxAllowedAirUtil() && config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN) {
        LOG_INFO("Sending our nodeinfo to mesh (wantReplies=%d)\n", requestReplies);
        sendOurNodeInfo(NODENUM_BROADCAST, requestReplies); // Send our info (don't request replies)
    }

    return getConfiguredOrDefaultMs(config.device.node_info_broadcast_secs, default_broadcast_interval_secs);
}
