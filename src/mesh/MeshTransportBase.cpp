#include "mesh/MeshTransportBase.h"
#include "DebugConfiguration.h"
#include "mesh/NodeDB.h"
#include <algorithm>
#include <cstring>

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

    for (auto *t : *preEncodeTransports) {
        if (t->isEnabled())
            t->onSendPreEncode(mp_encrypted, mp_decoded, chIndex);
    }
}

namespace
{
// Fields the LoRa header does not carry, so a radio arrival has them at their defaults. Left as a
// sender set them, priority MAX outranks the ACK ceiling and tx_after schedules our transmit.
void clearLocalOnly(meshtastic_MeshPacket &mp)
{
    mp.tx_after = 0;
    mp.priority = meshtastic_MeshPacket_Priority_UNSET;
    // Router re-establishes these after a PKI decrypt; Router::send reads the key bytes without the size.
    mp.pki_encrypted = false;
    mp.public_key.size = 0;
    memset(mp.public_key.bytes, 0, sizeof(mp.public_key.bytes));
    mp.rx_snr = 0;
    mp.rx_rssi = 0;
    mp.has_rx_rssi = false;
    mp.rx_time = 0;
    mp.has_rx_time = false;
}
} // namespace

bool MeshTransportBase::sanitizeIngress(meshtastic_MeshPacket &mp, meshtastic_MeshPacket_TransportMechanism medium)
{
    // A local origin reaches paths that trust isFromUs, remote admin among them.
    if (isFromUs(&mp)) {
        LOG_WARN("Transport %d: packet with local from=0x%08x, dropping", medium, mp.from);
        return false;
    }
    if (mp.hop_limit > HOP_MAX || mp.hop_start > HOP_MAX) {
        LOG_WARN("Transport %d: invalid hop_limit(%u) or hop_start(%u), dropping", medium, mp.hop_limit, mp.hop_start);
        return false;
    }
    mp.transport_mechanism = medium;
    clearLocalOnly(mp);
    return true;
}

meshtastic_MeshPacket MeshTransportBase::stripForTransmit(const meshtastic_MeshPacket &mp)
{
    meshtastic_MeshPacket out = mp;
    out.transport_mechanism = meshtastic_MeshPacket_TransportMechanism_TRANSPORT_INTERNAL;
    clearLocalOnly(out);
    return out;
}
