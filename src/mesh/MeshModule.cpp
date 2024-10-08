#include "MeshModule.h"
#include "Channels.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "configuration.h"
#include "modules/RoutingModule.h"
#include <assert.h>

std::vector<MeshModule *> *MeshModule::modules;

const meshtastic_MeshPacket *MeshModule::currentRequest;

/**
 * If any of the current chain of modules has already sent a reply, it will be here.  This is useful to allow
 * the RoutingModule to avoid sending redundant acks
 */
meshtastic_MeshPacket *MeshModule::currentReply;

MeshModule::MeshModule(const char *_name) : name(_name)
{
    // Can't trust static initializer order, so we check each time
    if (!modules)
        modules = new std::vector<MeshModule *>();

    modules->push_back(this);
}

void MeshModule::setup() {}

MeshModule::~MeshModule()
{
    assert(0); // FIXME - remove from list of modules once someone needs this feature
}

meshtastic_MeshPacket *MeshModule::allocAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex,
                                               uint8_t hopStart, uint8_t hopLimit)
{
    meshtastic_Routing c = meshtastic_Routing_init_default;

    c.error_reason = err;
    c.which_variant = meshtastic_Routing_error_reason_tag;

    // Now that we have moded sendAckNak up one level into the class hierarchy we can no longer assume we are a RoutingModule
    // So we manually call pb_encode_to_bytes and specify routing port number
    // auto p = allocDataProtobuf(c);
    meshtastic_MeshPacket *p = router->allocForSending();
    p->decoded.portnum = meshtastic_PortNum_ROUTING_APP;
    p->decoded.payload.size =
        pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_Routing_msg, &c);

    p->priority = meshtastic_MeshPacket_Priority_ACK;

    p->hop_limit = routingModule->getHopLimitForResponse(hopStart, hopLimit); // Flood ACK back to original sender
    p->to = to;
    p->decoded.request_id = idFrom;
    p->channel = chIndex;
    if (err != meshtastic_Routing_Error_NONE)
        LOG_WARN("Alloc an err=%d,to=0x%x,idFrom=0x%x,id=0x%x\n", err, to, idFrom, p->id);

    return p;
}

meshtastic_MeshPacket *MeshModule::allocErrorResponse(meshtastic_Routing_Error err, const meshtastic_MeshPacket *p)
{
    // If the original packet couldn't be decoded, use the primary channel
    uint8_t channelIndex =
        p->which_payload_variant == meshtastic_MeshPacket_decoded_tag ? p->channel : channels.getPrimaryIndex();
    auto r = allocAckNak(err, getFrom(p), p->id, channelIndex);

    setReplyTo(r, *p);

    return r;
}

void MeshModule::callModules(meshtastic_MeshPacket &mp, RxSource src)
{
    // LOG_DEBUG("In call modules\n");
    bool moduleFound = false;

    // We now allow **encrypted** packets to pass through the modules
    bool isDecoded = mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag;

    currentReply = NULL; // No reply yet

    bool ignoreRequest = false; // No module asked to ignore the request yet

    // Was this message directed to us specifically?  Will be false if we are sniffing someone elses packets
    auto ourNodeNum = nodeDB->getNodeNum();
    bool toUs = mp.to == NODENUM_BROADCAST || isToUs(&mp);

    for (auto i = modules->begin(); i != modules->end(); ++i) {
        auto &pi = **i;

        pi.currentRequest = &mp;

        /// We only call modules that are interested in the packet (and the message is destined to us or we are promiscious)
        bool wantsPacket = (isDecoded || pi.encryptedOk) && (pi.isPromiscuous || toUs) && pi.wantPacket(&mp);

        if ((src == RX_SRC_LOCAL) && !(pi.loopbackOk)) {
            // new case, monitor separately for now, then FIXME merge above
            wantsPacket = false;
        }

        assert(!pi.myReply); // If it is !null it means we have a bug, because it should have been sent the previous time

        if (wantsPacket) {
            LOG_DEBUG("Module '%s' wantsPacket=%d\n", pi.name, wantsPacket);

            moduleFound = true;

            /// received channel (or NULL if not decoded)
            meshtastic_Channel *ch = isDecoded ? &channels.getByIndex(mp.channel) : NULL;

            /// Is the channel this packet arrived on acceptable? (security check)
            /// Note: we can't know channel names for encrypted packets, so those are NEVER sent to boundChannel modules

            /// Also: if a packet comes in on the local PC interface, we don't check for bound channels, because it is TRUSTED and
            /// it needs to to be able to fetch the initial admin packets without yet knowing any channels.

            bool rxChannelOk = !pi.boundChannel || (mp.from == 0) || (ch && strcasecmp(ch->settings.name, pi.boundChannel) == 0);

            if (!rxChannelOk) {
                // no one should have already replied!
                assert(!currentReply);

                if (isDecoded && mp.decoded.want_response) {
                    printPacket("packet on wrong channel, returning error", &mp);
                    currentReply = pi.allocErrorResponse(meshtastic_Routing_Error_NOT_AUTHORIZED, &mp);
                } else
                    printPacket("packet on wrong channel, but can't respond", &mp);
            } else {
                ProcessMessage handled = pi.handleReceived(mp);

                pi.alterReceived(mp);

                // Possibly send replies (but only if the message was directed to us specifically, i.e. not for promiscious
                // sniffing) also: we only let the one module send a reply, once that happens, remaining modules are not
                // considered

                // NOTE: we send a reply *even if the (non broadcast) request was from us* which is unfortunate but necessary
                // because currently when the phone sends things, it sends things using the local node ID as the from address.  A
                // better solution (FIXME) would be to let phones have their own distinct addresses and we 'route' to them like
                // any other node.
                if (isDecoded && mp.decoded.want_response && toUs && (!isFromUs(&mp) || isToUs(&mp)) && !currentReply) {
                    pi.sendResponse(mp);
                    ignoreRequest = ignoreRequest || pi.ignoreRequest; // If at least one module asks it, we may ignore a request
                    LOG_INFO("Asked module '%s' to send a response\n", pi.name);
                } else {
                    LOG_DEBUG("Module '%s' considered\n", pi.name);
                }

                // If the requester didn't ask for a response we might need to discard unused replies to prevent memory leaks
                if (pi.myReply) {
                    LOG_DEBUG("Discarding an unneeded response\n");
                    packetPool.release(pi.myReply);
                    pi.myReply = NULL;
                }

                if (handled == ProcessMessage::STOP) {
                    LOG_DEBUG("Module '%s' handled and skipped other processing\n", pi.name);
                    break;
                }
            }
        }

        pi.currentRequest = NULL;
    }

    if (isDecoded && mp.decoded.want_response && toUs) {
        if (currentReply) {
            printPacket("Sending response", currentReply);
            service->sendToMesh(currentReply);
            currentReply = NULL;
        } else if (mp.from != ourNodeNum && !ignoreRequest) {
            // Note: if the message started with the local node or a module asked to ignore the request, we don't want to send a
            // no response reply

            // No one wanted to reply to this request, tell the requster that happened
            LOG_DEBUG("No one responded, send a nak\n");

            // SECURITY NOTE! I considered sending back a different error code if we didn't find the psk (i.e. !isDecoded)
            // but opted NOT TO.  Because it is not a good idea to let remote nodes 'probe' to find out which PSKs were "good" vs
            // bad.
            routingModule->sendAckNak(meshtastic_Routing_Error_NO_RESPONSE, getFrom(&mp), mp.id, mp.channel, mp.hop_start,
                                      mp.hop_limit);
        }
    }

    if (!moduleFound && isDecoded) {
        LOG_DEBUG("No modules interested in portnum=%d, src=%s\n", mp.decoded.portnum,
                  (src == RX_SRC_LOCAL) ? "LOCAL" : "REMOTE");
    }
}

meshtastic_MeshPacket *MeshModule::allocReply()
{
    auto r = myReply;
    myReply = NULL; // Only use each reply once
    return r;
}

/** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
 * so that subclasses can (optionally) send a response back to the original sender.  Implementing this method
 * is optional
 */
void MeshModule::sendResponse(const meshtastic_MeshPacket &req)
{
    auto r = allocReply();
    if (r) {
        setReplyTo(r, req);
        currentReply = r;
    } else {
        // Ignore - this is now expected behavior for routing module (because it ignores some replies)
        // LOG_WARN("Client requested response but this module did not provide\n");
    }
}

/** set the destination and packet parameters of packet p intended as a reply to a particular "to" packet
 * This ensures that if the request packet was sent reliably, the reply is sent that way as well.
 */
void setReplyTo(meshtastic_MeshPacket *p, const meshtastic_MeshPacket &to)
{
    assert(p->which_payload_variant == meshtastic_MeshPacket_decoded_tag); // Should already be set by now
    p->to = getFrom(&to);    // Make sure that if we are sending to the local node, we use our local node addr, not 0
    p->channel = to.channel; // Use the same channel that the request came in on
    p->hop_limit = routingModule->getHopLimitForResponse(to.hop_start, to.hop_limit);

    // No need for an ack if we are just delivering locally (it just generates an ignored ack)
    p->want_ack = (to.from != 0) ? to.want_ack : false;
    if (p->priority == meshtastic_MeshPacket_Priority_UNSET)
        p->priority = meshtastic_MeshPacket_Priority_RELIABLE;
    p->decoded.request_id = to.id;
}

std::vector<MeshModule *> MeshModule::GetMeshModulesWithUIFrames()
{

    std::vector<MeshModule *> modulesWithUIFrames;
    if (modules) {
        for (auto i = modules->begin(); i != modules->end(); ++i) {
            auto &pi = **i;
            if (pi.wantUIFrame()) {
                LOG_DEBUG("%s wants a UI Frame\n", pi.name);
                modulesWithUIFrames.push_back(&pi);
            }
        }
    }
    return modulesWithUIFrames;
}

void MeshModule::observeUIEvents(Observer<const UIFrameEvent *> *observer)
{
    if (modules) {
        for (auto i = modules->begin(); i != modules->end(); ++i) {
            auto &pi = **i;
            Observable<const UIFrameEvent *> *observable = pi.getUIFrameObservable();
            if (observable != NULL) {
                LOG_DEBUG("%s wants a UI Frame\n", pi.name);
                observer->observe(observable);
            }
        }
    }
}

AdminMessageHandleResult MeshModule::handleAdminMessageForAllModules(const meshtastic_MeshPacket &mp,
                                                                     meshtastic_AdminMessage *request,
                                                                     meshtastic_AdminMessage *response)
{
    AdminMessageHandleResult handled = AdminMessageHandleResult::NOT_HANDLED;
    if (modules) {
        for (auto i = modules->begin(); i != modules->end(); ++i) {
            auto &pi = **i;
            AdminMessageHandleResult h = pi.handleAdminMessageForModule(mp, request, response);
            if (h == AdminMessageHandleResult::HANDLED_WITH_RESPONSE) {
                // In case we have a response it always has priority.
                LOG_DEBUG("Reply prepared by module '%s' of variant: %d\n", pi.name, response->which_payload_variant);
                handled = h;
            } else if ((handled != AdminMessageHandleResult::HANDLED_WITH_RESPONSE) && (h == AdminMessageHandleResult::HANDLED)) {
                // In case the message is handled it should be populated, but will not overwrite
                //   a result with response.
                handled = h;
            }
        }
    }
    return handled;
}

#if HAS_SCREEN
// Would our module like its frame to be focused after Screen::setFrames has regenerated the list of frames?
// Only considered if setFrames is triggered by a UIFrameEvent
bool MeshModule::isRequestingFocus()
{
    if (_requestingFocus) {
        _requestingFocus = false; // Consume the request
        return true;
    } else
        return false;
}
#endif
