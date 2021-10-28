#include "configuration.h"
#include "MeshPlugin.h"
#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "plugins/RoutingPlugin.h"
#include <assert.h>

std::vector<MeshPlugin *> *MeshPlugin::plugins;

const MeshPacket *MeshPlugin::currentRequest;

/**
 * If any of the current chain of plugins has already sent a reply, it will be here.  This is useful to allow
 * the RoutingPlugin to avoid sending redundant acks
 */
MeshPacket *MeshPlugin::currentReply;

MeshPlugin::MeshPlugin(const char *_name) : name(_name)
{
    // Can't trust static initalizer order, so we check each time
    if (!plugins)
        plugins = new std::vector<MeshPlugin *>();

    plugins->push_back(this);
}

void MeshPlugin::setup() {}

MeshPlugin::~MeshPlugin()
{
    assert(0); // FIXME - remove from list of plugins once someone needs this feature
}

MeshPacket *MeshPlugin::allocAckNak(Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex)
{
    Routing c = Routing_init_default;

    c.error_reason = err;
    c.which_variant = Routing_error_reason_tag;

    // Now that we have moded sendAckNak up one level into the class heirarchy we can no longer assume we are a RoutingPlugin
    // So we manually call pb_encode_to_bytes and specify routing port number
    // auto p = allocDataProtobuf(c);
    MeshPacket *p = router->allocForSending();
    p->decoded.portnum = PortNum_ROUTING_APP;
    p->decoded.payload.size = pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), Routing_fields, &c);

    p->priority = MeshPacket_Priority_ACK;

    p->hop_limit = 0; // Assume just immediate neighbors for now
    p->to = to;
    p->decoded.request_id = idFrom;
    p->channel = chIndex;
    DEBUG_MSG("Alloc an err=%d,to=0x%x,idFrom=0x%x,id=0x%x\n", err, to, idFrom, p->id);

    return p;
}

MeshPacket *MeshPlugin::allocErrorResponse(Routing_Error err, const MeshPacket *p)
{
    auto r = allocAckNak(err, getFrom(p), p->id, p->channel);

    setReplyTo(r, *p);

    return r;
}

void MeshPlugin::callPlugins(const MeshPacket &mp, RxSource src)
{
    // DEBUG_MSG("In call plugins\n");
    bool pluginFound = false;

    // We now allow **encrypted** packets to pass through the plugins
    bool isDecoded = mp.which_payloadVariant == MeshPacket_decoded_tag;

    currentReply = NULL; // No reply yet

    // Was this message directed to us specifically?  Will be false if we are sniffing someone elses packets
    auto ourNodeNum = nodeDB.getNodeNum();
    bool toUs = mp.to == NODENUM_BROADCAST || mp.to == ourNodeNum;

    for (auto i = plugins->begin(); i != plugins->end(); ++i) {
        auto &pi = **i;

        pi.currentRequest = &mp;

        /// We only call plugins that are interested in the packet (and the message is destined to us or we are promiscious)
        bool wantsPacket = (isDecoded || pi.encryptedOk) && (pi.isPromiscuous || toUs) && pi.wantPacket(&mp);

        if ((src == RX_SRC_LOCAL) && !(pi.loopbackOk)) {
            // new case, monitor separately for now, then FIXME merge above
            wantsPacket = false;
        }

        assert(!pi.myReply); // If it is !null it means we have a bug, because it should have been sent the previous time

        if (wantsPacket) {
            DEBUG_MSG("Plugin %s wantsPacket=%d\n", pi.name, wantsPacket);

            pluginFound = true;

            /// received channel (or NULL if not decoded)
            Channel *ch = isDecoded ? &channels.getByIndex(mp.channel) : NULL;

            /// Is the channel this packet arrived on acceptable? (security check)
            /// Note: we can't know channel names for encrypted packets, so those are NEVER sent to boundChannel plugins

            /// Also: if a packet comes in on the local PC interface, we don't check for bound channels, because it is TRUSTED and it needs to
            /// to be able to fetch the initial admin packets without yet knowing any channels.

            bool rxChannelOk = !pi.boundChannel || (mp.from == 0) || (ch && (strcmp(ch->settings.name, pi.boundChannel) == 0));

            if (!rxChannelOk) {
                // no one should have already replied!
                assert(!currentReply);

                if (mp.decoded.want_response) {
                    printPacket("packet on wrong channel, returning error", &mp);
                    currentReply = pi.allocErrorResponse(Routing_Error_NOT_AUTHORIZED, &mp);
                } else
                    printPacket("packet on wrong channel, but can't respond", &mp);
            } else {

                ProcessMessage handled = pi.handleReceived(mp);

                // Possibly send replies (but only if the message was directed to us specifically, i.e. not for promiscious
                // sniffing) also: we only let the one plugin send a reply, once that happens, remaining plugins are not
                // considered

                // NOTE: we send a reply *even if the (non broadcast) request was from us* which is unfortunate but necessary
                // because currently when the phone sends things, it sends things using the local node ID as the from address.  A
                // better solution (FIXME) would be to let phones have their own distinct addresses and we 'route' to them like
                // any other node.
                if (mp.decoded.want_response && toUs && (getFrom(&mp) != ourNodeNum || mp.to == ourNodeNum) && !currentReply) {
                    pi.sendResponse(mp);
                    DEBUG_MSG("Plugin %s sent a response\n", pi.name);
                } else {
                    DEBUG_MSG("Plugin %s considered\n", pi.name);
                }

                // If the requester didn't ask for a response we might need to discard unused replies to prevent memory leaks
                if (pi.myReply) {
                    DEBUG_MSG("Discarding an unneeded response\n");
                    packetPool.release(pi.myReply);
                    pi.myReply = NULL;
                }

                if (handled == ProcessMessage::STOP) {
                    DEBUG_MSG("Plugin %s handled and skipped other processing\n", pi.name);
                    break;
                }
            }
        }

        pi.currentRequest = NULL;
    }

    if (mp.decoded.want_response && toUs) {
        if (currentReply) {
            printPacket("Sending response", currentReply);
            service.sendToMesh(currentReply);
            currentReply = NULL;
        } else if(mp.from != ourNodeNum) {
            // Note: if the message started with the local node we don't want to send a no response reply

            // No one wanted to reply to this requst, tell the requster that happened
            DEBUG_MSG("No one responded, send a nak\n");

            // SECURITY NOTE! I considered sending back a different error code if we didn't find the psk (i.e. !isDecoded)
            // but opted NOT TO.  Because it is not a good idea to let remote nodes 'probe' to find out which PSKs were "good" vs
            // bad.
            routingPlugin->sendAckNak(Routing_Error_NO_RESPONSE, getFrom(&mp), mp.id, mp.channel);
        }
    }

    if (!pluginFound)
        DEBUG_MSG("No plugins interested in portnum=%d, src=%s\n",
                    mp.decoded.portnum, 
                    (src == RX_SRC_LOCAL) ? "LOCAL":"REMOTE");
}

MeshPacket *MeshPlugin::allocReply()
{
    auto r = myReply;
    myReply = NULL; // Only use each reply once
    return r;
}

/** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
 * so that subclasses can (optionally) send a response back to the original sender.  Implementing this method
 * is optional
 */
void MeshPlugin::sendResponse(const MeshPacket &req)
{
    auto r = allocReply();
    if (r) {
        setReplyTo(r, req);
        currentReply = r;
    } else {
        // Ignore - this is now expected behavior for routing plugin (because it ignores some replies)
        // DEBUG_MSG("WARNING: Client requested response but this plugin did not provide\n");
    }
}

/** set the destination and packet parameters of packet p intended as a reply to a particular "to" packet
 * This ensures that if the request packet was sent reliably, the reply is sent that way as well.
 */
void setReplyTo(MeshPacket *p, const MeshPacket &to)
{
    assert(p->which_payloadVariant == MeshPacket_decoded_tag); // Should already be set by now
    p->to = getFrom(&to);    // Make sure that if we are sending to the local node, we use our local node addr, not 0
    p->channel = to.channel; // Use the same channel that the request came in on

    // No need for an ack if we are just delivering locally (it just generates an ignored ack)
    p->want_ack = (to.from != 0) ? to.want_ack : false;
    if (p->priority == MeshPacket_Priority_UNSET)
        p->priority = MeshPacket_Priority_RELIABLE;
    p->decoded.request_id = to.id;
}

std::vector<MeshPlugin *> MeshPlugin::GetMeshPluginsWithUIFrames()
{

    std::vector<MeshPlugin *> pluginsWithUIFrames;
    for (auto i = plugins->begin(); i != plugins->end(); ++i) {
        auto &pi = **i;
        if (pi.wantUIFrame()) {
            DEBUG_MSG("Plugin wants a UI Frame\n");
            pluginsWithUIFrames.push_back(&pi);
        }
    }
    return pluginsWithUIFrames;
}
