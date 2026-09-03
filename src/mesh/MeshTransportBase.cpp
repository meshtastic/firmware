#include "mesh/MeshTransportBase.h"
#include <algorithm>

std::vector<MeshTransportBase *> *MeshTransportBase::transports;

MeshTransportBase::MeshTransportBase()
{
    // Can't trust static initializer order, so we check each time (same as MeshModule).
    if (!transports)
        transports = new std::vector<MeshTransportBase *>();

    transports->push_back(this);
}

MeshTransportBase::~MeshTransportBase()
{
    if (transports) {
        auto it = std::find(transports->begin(), transports->end(), this);
        if (it != transports->end())
            transports->erase(it);
    }
}

void MeshTransportBase::callTransports(const meshtastic_MeshPacket *mp)
{
    if (!transports)
        return;

    // Every enabled transport gets every packet; a transport accepting it never suppresses another.
    for (auto *t : *transports) {
        if (t->isEnabled())
            t->onSend(mp);
    }
}
