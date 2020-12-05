#include "MeshPlugin.h"
#include "NodeDB.h"
#include <assert.h>

std::vector<MeshPlugin *> *MeshPlugin::plugins;

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
    for (auto i = plugins->begin(); i != plugins->end(); ++i) {
        auto &pi = **i;
        if (pi.wantPortnum(mp.decoded.data.portnum)) {
            bool handled = pi.handleReceived(mp);

            // Possibly send replies (unless we are handling a locally generated message)
            if (mp.decoded.want_response && mp.from != nodeDB.getNodeNum())
                pi.sendResponse(mp.from);

            DEBUG_MSG("Plugin %s handled=%d\n", pi.name, handled);
            if (handled)
                break;
        }
        else {
            DEBUG_MSG("Plugin %s not interested\n", pi.name);
        }
    }
}