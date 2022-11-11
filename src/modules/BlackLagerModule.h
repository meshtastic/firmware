#pragma once
#include "SinglePortModule.h"

/**
 * A simple example module that just replies with "Message received" to any message it receives.
 */
class BlackLagerModule : public SinglePortModule
{
  public:
    /** Constructor
     * name is for debugging output
     */
    BlackLagerModule() : SinglePortModule("reply", PortNum_PRIVATE_APP) {}

  protected:
    /** For reply module we do all of our processing in the (normally optional)
     * want_replies handling
     */
    virtual MeshPacket *allocReply() override;
};

extern BlackLagerModule *blackLagerModule;
