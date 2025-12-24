#include "PhoneAPIModule.h"
#include "MeshService.h"
#include "Router.h"
#include "configuration.h"

PhoneAPIModule *phoneAPIModule;

PhoneAPIModule::PhoneAPIModule() : MeshModule("phoneapi") {
  // Not promiscuous - only interested in packets destined for us or broadcasts
  isPromiscuous = false;
  encryptedOk = true;
}

bool PhoneAPIModule::wantPacket(const meshtastic_MeshPacket *p) {
  // We want broadcasts or packets specifically addressed to us
  // Exclude packets that originated from this node (from == 0 means local)
  return (isBroadcast(p->to) || isToUs(p)) && (p->from != 0);
}

ProcessMessage PhoneAPIModule::handleReceived(const meshtastic_MeshPacket &mp) {
  // Deliver the packet to connected phone/API clients
  printPacket("Delivering rx packet", &mp);
  service->handleFromRadio(&mp);

  return ProcessMessage::CONTINUE;
}
