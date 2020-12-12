#pragma once
#include "SinglePortPlugin.h"


/**
 * A simple example plugin that just replies with "Message received" to any message it receives.
 */
class ReplyPlugin : public SinglePortPlugin
{
  public:
    /** Constructor
     * name is for debugging output
     */
    ReplyPlugin() : SinglePortPlugin("reply", PortNum_REPLY_APP) {}

  protected:

    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceived(const MeshPacket &mp);
};
