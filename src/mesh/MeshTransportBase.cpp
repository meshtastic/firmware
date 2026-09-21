#include "mesh/MeshTransportBase.h"
#include <algorithm>

std::vector<MeshTransportBase *> *MeshTransportBase::postEncodeTransports;
std::vector<MeshTransportBase *> *MeshTransportBase::preEncodeTransports;

MeshTransportBase::MeshTransportBase(HookPoint hook) : hookPoint(hook)
{
    // Static initializer order is not guaranteed, so the list is created on first use (as MeshModule does).
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

    for (auto *t : *postEncodeTransports) {
        if (t->isEnabled())
            t->onSend(mp);
    }
}

bool MeshTransportBase::cancelTransportsOn(meshtastic_MeshPacket_TransportMechanism medium, NodeNum from, PacketId id)
{
    if (!postEncodeTransports)
        return false;

    // No isEnabled() gate: a transport disabled since queueing still holds the frame.
    bool canceled = false;
    for (auto *t : *postEncodeTransports)
        canceled |= t->onCancelSending(medium, from, id);
    return canceled;
}

void MeshTransportBase::callTransportsPreEncode(const meshtastic_MeshPacket &mp_encrypted,
                                                const meshtastic_MeshPacket &mp_decoded, ChannelIndex chIndex)
{
    if (!preEncodeTransports)
        return;

    // No isEnabled() gate: the call site applies the transport-specific gate.
    for (auto *t : *preEncodeTransports)
        t->onSendPreEncode(mp_encrypted, mp_decoded, chIndex);
}
