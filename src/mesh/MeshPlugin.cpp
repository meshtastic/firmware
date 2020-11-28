#include "MeshPlugin.h"
#include <assert.h>

std::vector<MeshPlugin *> MeshPlugin::plugins;

MeshPlugin::MeshPlugin(const char *_name) : name(_name) {
    plugins.push_back(this);
}

MeshPlugin::~MeshPlugin()
{
    assert(0); // FIXME - remove from list of plugins once someone needs this feature
}

void MeshPlugin::callPlugins(const MeshPacket &mp)
{
    for (auto i = plugins.begin(); i != plugins.end(); ++i) {
        if ((*i)->wantPortnum(mp.decoded.data.portnum))
            if ((*i)->handleReceived(mp))
                break;
    }
}