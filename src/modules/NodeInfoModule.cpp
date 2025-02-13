#include "NodeInfoModule.h"
#include "Default.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"
#include <Throttle.h>

NodeInfoModule *nodeInfoModule;

bool NodeInfoModule::handleReceivedProtobuf(const meshtastic_MeshPacket &mp, meshtastic_User *pptr)
{
    auto p = *pptr;

    bool hasChanged = nodeDB->updateUser(getFrom(&mp), p, mp.channel);

    bool wasBroadcast = isBroadcast(mp.to);

    // Show new nodes on LCD screen
    if (wasBroadcast) {
        String lcd = String("Joined: ") + p.long_name + "\n";
        if (screen)
            screen->print(lcd.c_str());
    }

    // if user has changed while packet was not for us, inform phone
    if (hasChanged && !wasBroadcast && !isToUs(&mp))
        service->sendToPhone(packetPool.allocCopy(mp));

    // LOG_DEBUG("did handleReceived");
    return false; // Let others look at this message also if they want
}

void NodeInfoModule::sendOurNodeInfo(NodeNum dest, bool wantReplies, uint8_t channel, bool _shorterTimeout)
{
    // cancel any not yet sent (now stale) position packets
    if (prevPacketId) // if we wrap around to zero, we'll simply fail to cancel in that rare case (no big deal)
        service->cancelSending(prevPacketId);
    shorterTimeout = _shorterTimeout;
    meshtastic_MeshPacket *p = allocReply();
    if (p) { // Check whether we didn't ignore it
        p->to = dest;
        p->decoded.want_response = (config.device.role != meshtastic_Config_DeviceConfig_Role_TRACKER &&
                                    config.device.role != meshtastic_Config_DeviceConfig_Role_SENSOR) &&
                                   wantReplies;
        if (_shorterTimeout)
            p->priority = meshtastic_MeshPacket_Priority_DEFAULT;
        else
            p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
        if (channel > 0) {
            LOG_DEBUG("Send ourNodeInfo to channel %d", channel);
            p->channel = channel;
        }

        prevPacketId = p->id;

        service->sendToMesh(p);
        shorterTimeout = false;
    }
}

meshtastic_MeshPacket *NodeInfoModule::allocReply()
{
    if (!airTime->isTxAllowedChannelUtil(false)) {
        ignoreRequest = true; // Mark it as ignored for MeshModule
        LOG_DEBUG("Skip send NodeInfo > 40%% ch. util");
        return NULL;
    }
    // If we sent our NodeInfo less than 5 min. ago, don't send it again as it may be still underway.
    if (!shorterTimeout && lastSentToMesh && Throttle::isWithinTimespanMs(lastSentToMesh, 5 * 60 * 1000)) {
        LOG_DEBUG("Skip send NodeInfo since we sent it <5min ago");
        ignoreRequest = true; // Mark it as ignored for MeshModule
        return NULL;
    } else if (shorterTimeout && lastSentToMesh && Throttle::isWithinTimespanMs(lastSentToMesh, 60 * 1000)) {
        LOG_DEBUG("Skip send NodeInfo since we sent it <60s ago");
        ignoreRequest = true; // Mark it as ignored for MeshModule
        return NULL;
    } else {
        ignoreRequest = false; // Don't ignore requests anymore
        meshtastic_User &u = owner;

        // Strip the public key if the user is licensed
        if (u.is_licensed && u.public_key.size > 0) {
            u.public_key.bytes[0] = 0;
            u.public_key.size = 0;
        }

        LOG_INFO("Send owner %s/%s/%s", u.id, u.long_name, u.short_name);
        lastSentToMesh = millis();
        return allocDataProtobuf(u);
    }
}

NodeInfoModule::NodeInfoModule()
    : ProtobufModule("nodeinfo", meshtastic_PortNum_NODEINFO_APP, &meshtastic_User_msg), concurrency::OSThread("NodeInfo")
{
    isPromiscuous = true; // We always want to update our nodedb, even if we are sniffing on others

    setIntervalFromNow(setStartDelay()); // Send our initial owner announcement 30 seconds
                                         // after we start (to give network time to setup)
}

int32_t NodeInfoModule::runOnce()
{
    // If we changed channels, ask everyone else for their latest info
    bool requestReplies = currentGeneration != radioGeneration;
    currentGeneration = radioGeneration;

    if (airTime->isTxAllowedAirUtil() && config.device.role != meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN) {
        LOG_INFO("Send our nodeinfo to mesh (wantReplies=%d)", requestReplies);
        sendOurNodeInfo(NODENUM_BROADCAST, requestReplies); // Send our info (don't request replies)
    }
    return Default::getConfiguredOrDefaultMs(config.device.node_info_broadcast_secs, default_node_info_broadcast_secs);
}