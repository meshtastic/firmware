#pragma once
#include "SinglePortModule.h"

/**
 * A simple example module that just replies with "Message received" to any message it receives.
 */
class ReplyModule : public SinglePortModule
{
  public:
    /** Constructor
     * name is for debugging output
     */
    ReplyModule() : SinglePortModule("reply", meshtastic_PortNum_REPLY_APP) {}

  protected:
    /** For reply module we do all of our processing in the (normally optional)
     * want_replies handling
     */
    virtual meshtastic_MeshPacket *allocReply() override;
};
