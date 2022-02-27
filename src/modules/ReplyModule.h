#pragma once
#include "SinglePortPlugin.h"


/**
 * A simple example module that just replies with "Message received" to any message it receives.
 */
class ReplyModule : public SinglePortPlugin
{
  public:
    /** Constructor
     * name is for debugging output
     */
    ReplyModule() : SinglePortPlugin("reply", PortNum_REPLY_APP) {}

  protected:

    /** For reply module we do all of our processing in the (normally optional)
     * want_replies handling
    */
    virtual MeshPacket *allocReply() override;
};
