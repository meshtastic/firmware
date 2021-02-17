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
    for (auto i = plugins->begin(); i != plugins->end(); ++i) {
        auto &pi = **i;

        pi.currentRequest = &mp;
        if (pi.wantPortnum(mp.decoded.portnum)) {
            pluginFound = true;

            bool handled = pi.handleReceived(mp);

            // Possibly send replies
            if (mp.decoded.want_response)
                pi.sendResponse(mp);

            DEBUG_MSG("Plugin %s handled=%d\n", pi.name, handled);
            if (handled)
                break;
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
        DEBUG_MSG("WARNING: Client requested response but this plugin did not provide\n");
    }
}

/** set the destination and packet parameters of packet p intended as a reply to a particular "to" packet 
 * This ensures that if the request packet was sent reliably, the reply is sent that way as well.
*/
void setReplyTo(MeshPacket *p, const MeshPacket &to) {
    p->to = to.from;
    p->want_ack = to.want_ack;
}