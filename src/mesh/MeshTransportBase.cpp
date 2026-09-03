#include "mesh/MeshTransportBase.h"
#include <algorithm>

std::vector<MeshTransportBase *> *MeshTransportBase::postEncodeTransports;
std::vector<MeshTransportBase *> *MeshTransportBase::preEncodeTransports;

MeshTransportBase::MeshTransportBase(HookPoint hook) : hookPoint(hook)
{
    // Can't trust static initializer order, so we check each time (same as MeshModule).
    std::vector<MeshTransportBase *> *&list = (hook == PreEncode) ? preEncodeTransports : postEncodeTransports;
    if (!list)
        list = new std::vector<MeshTransportBase *>();

    list->push_back(this);
}

MeshTransportBase::~MeshTransportBase()
{
    std::vector<MeshTransportBase *> *list = (hookPoint == PreEncode) ? preEncodeTransports : postEncodeTransports;
    if (list) {
        auto it = std::find(list->begin(), list->end(), this);
        if (it != list->end())
            list->erase(it);
    }
}

void MeshTransportBase::callTransports(const meshtastic_MeshPacket *mp)
{
    if (!postEncodeTransports)
        return;

    // Every enabled transport gets every packet; a transport accepting it never suppresses another.
    for (auto *t : *postEncodeTransports) {
        if (t->isEnabled())
            t->onSend(mp);
    }
}

void MeshTransportBase::callTransportsPreEncode(const meshtastic_MeshPacket &mp_encrypted,
                                                const meshtastic_MeshPacket &mp_decoded, ChannelIndex chIndex)
{
    if (!preEncodeTransports)
        return;

    // No isEnabled() gate here (see header): the call site applies the transport-specific gate, each
    // transport applies the rest of its policy inside onSendPreEncode.
    for (auto *t : *preEncodeTransports)
        t->onSendPreEncode(mp_encrypted, mp_decoded, chIndex);
}
