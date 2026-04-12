#pragma once
#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_REPLYBOT
#include "SinglePortModule.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

class ReplyBotModule : public SinglePortModule
{
  public:
    ReplyBotModule();
    void setup() override;
    bool wantPacket(const meshtastic_MeshPacket *p) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  protected:
    bool isCommand(const char *msg) const;
    void sendDm(const meshtastic_MeshPacket &rx, const char *text);
};
#endif // MESHTASTIC_EXCLUDE_REPLYBOT