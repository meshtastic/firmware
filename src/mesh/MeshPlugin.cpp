#include "MeshPlugin.h"
#include "NodeDB.h"
#include "MeshService.h"
#include <assert.h>

std::vector<MeshPlugin *> *MeshPlugin::plugins;

const MeshPacket *MeshPlugin::currentRequest;

MeshPlugin::MeshPlugin(const char *_name) : name(_name)
{
    // Can't trust static initalizer order, so we check each time
    if(!plugins)
        plugins = new std::vector<MeshPlugin *>();

    plugins->push_back(this);
}

void MeshPlugin::setup() {
}

MeshPlugin::~MeshPlugin()
{
    assert(0); // FIXME - remove from list of plugins once someone needs this feature
}

void MeshPlugin::callPlugins(const MeshPacket &mp)
{
    // DEBUG_MSG("In call plugins\n");
    bool pluginFound = false;

    assert(mp.which_payloadVariant == MeshPacket_decoded_tag); // I think we are guarnteed the packet is decoded by this point?

    // Was this message directed to us specifically?  Will be false if we are sniffing someone elses packets
    auto ourNodeNum = nodeDB.getNodeNum();
    bool toUs = mp.to == NODENUM_BROADCAST || mp.to == ourNodeNum;
    for (auto i = plugins->begin(); i != plugins->end(); ++i) {
        auto &pi = **i;

        pi.currentRequest = &mp;

        // We only call plugins that are interested in the packet (and the message is destined to us or we are promiscious)
        bool wantsPacket = (pi.isPromiscuous || toUs) && pi.wantPacket(&mp);
        // DEBUG_MSG("Plugin %s wantsPacket=%d\n", pi.name, wantsPacket);
        if (wantsPacket) {
            pluginFound = true;

            bool handled = pi.handleReceived(mp);

            // Possibly send replies (but only if the message was directed to us specifically, i.e. not for promiscious sniffing)

            // NOTE: we send a reply *even if the (non broadcast) request was from us* which is unfortunate but necessary because currently when the phone 
            // sends things, it sends things using the local node ID as the from address.  A better solution (FIXME) would be to let phones
            // have their own distinct addresses and we 'route' to them like any other node.
            if (mp.decoded.want_response && toUs && (mp.from != ourNodeNum || mp.to == ourNodeNum)) {
                pi.sendResponse(mp);
                DEBUG_MSG("Plugin %s sent a response\n", pi.name);
            }
            else {
                DEBUG_MSG("Plugin %s considered\n", pi.name);
            }
            if (handled) {
                DEBUG_MSG("Plugin %s handled and skipped other processing\n", pi.name);
                break;
            }
        }
       
        pi.currentRequest = NULL;
    }

    if(!pluginFound)
        DEBUG_MSG("No plugins interested in portnum=%d\n", mp.decoded.portnum);
}

/** Messages can be received that have the want_response bit set.  If set, this callback will be invoked
 * so that subclasses can (optionally) send a response back to the original sender.  Implementing this method
 * is optional
 */
void MeshPlugin::sendResponse(const MeshPacket &req) {
    auto r = allocReply();
    if(r) {
        DEBUG_MSG("Sending response\n");
        setReplyTo(r, req);
        service.sendToMesh(r);
    }
    else {
        // Ignore - this is now expected behavior for routing plugin (because it ignores some replies)
        // DEBUG_MSG("WARNING: Client requested response but this plugin did not provide\n");
    }
}

/** set the destination and packet parameters of packet p intended as a reply to a particular "to" packet 
 * This ensures that if the request packet was sent reliably, the reply is sent that way as well.
*/
void setReplyTo(MeshPacket *p, const MeshPacket &to) {
    assert(p->which_payloadVariant == MeshPacket_decoded_tag); // Should already be set by now
    p->to = to.from;
    p->want_ack = to.want_ack;
    p->decoded.request_id = to.id;
}

std::vector<MeshPlugin *> MeshPlugin::GetMeshPluginsWithUIFrames() {

    std::vector<MeshPlugin *> pluginsWithUIFrames;    
    for (auto i = plugins->begin(); i != plugins->end(); ++i) {
        auto &pi = **i;
        if ( pi.wantUIFrame()) {
            DEBUG_MSG("Plugin wants a UI Frame\n");
            pluginsWithUIFrames.push_back(&pi);
        }
    }
    return pluginsWithUIFrames;
    
}
