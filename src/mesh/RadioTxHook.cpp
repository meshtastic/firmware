#include "RadioTxHook.h"

RadioTxHook *RadioTxHook::hookList = nullptr;

RadioTxHook::RadioTxHook()
{
    nextHook = hookList;
    hookList = this;
}

RadioTxHook::~RadioTxHook()
{
    for (RadioTxHook **slot = &hookList; *slot; slot = &(*slot)->nextHook) {
        if (*slot == this) {
            *slot = nextHook;
            break;
        }
    }
}

RadioTxHook::PreTxAction RadioTxHooks::beforeTransmit(RadioInterface *iface, meshtastic_MeshPacket *p)
{
    for (RadioTxHook *h = RadioTxHook::hookList; h; h = h->nextHook) {
        const RadioTxHook::PreTxAction action = h->beforeTransmit(iface, p);
        if (action != RadioTxHook::PRETX_SEND)
            return action;
    }
    return RadioTxHook::PRETX_SEND;
}

bool RadioTxHooks::holdsRadio(const meshtastic_MeshPacket *p)
{
    for (RadioTxHook *h = RadioTxHook::hookList; h; h = h->nextHook)
        if (h->holdsRadio(p))
            return true;
    return false;
}

void RadioTxHooks::packetReleased(RadioInterface *iface, const meshtastic_MeshPacket *p)
{
    for (RadioTxHook *h = RadioTxHook::hookList; h; h = h->nextHook)
        h->packetReleased(iface, p);
}
