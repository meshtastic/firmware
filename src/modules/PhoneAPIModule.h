#pragma once

#include "MeshModule.h"

/**
 * Module that delivers received packets to connected phone/API clients.
 *
 * This was extracted from RoutingModule to avoid having promiscuous
 * packet handling mixed with phone delivery logic.
 */
class PhoneAPIModule : public MeshModule {
public:
  PhoneAPIModule();

protected:
  virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
  virtual ProcessMessage
  handleReceived(const meshtastic_MeshPacket &mp) override;
};

extern PhoneAPIModule *phoneAPIModule;
