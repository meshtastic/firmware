#pragma once
#include "configuration.h"

#if !MESHTASTIC_EXCLUDE_EXAMPLE

#include "SinglePortModule.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

/**
 * Developer reference module.
 *
 * This module demonstrates the typical lifecycle of a Meshtastic firmware module:
 * construction in setupModules(), one-time setup(), packet filtering through
 * SinglePortModule, message handling in handleReceived(), and optional replies
 * through allocReply().
 *
 * It is excluded from normal builds by default. To enable it for local
 * development, remove or undefine MESHTASTIC_EXCLUDE_EXAMPLE in your build
 * configuration or variant.
 */
class ExampleModule : public SinglePortModule
{
  public:
    ExampleModule();

  protected:
    void setup() override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    meshtastic_MeshPacket *allocReply() override;

  private:
    uint32_t messageCount = 0;
};

#endif // MESHTASTIC_EXCLUDE_EXAMPLE
