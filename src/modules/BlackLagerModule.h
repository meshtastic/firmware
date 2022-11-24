#pragma once
#include "SinglePortModule.h"

/**
 * Black Lager module to send and receive public keys.
 */
class BlackLagerModule : public SinglePortModule
{
  public:
    /** Constructor
     * name is for debugging output
     */
    BlackLagerModule() : SinglePortModule("black-lager", PortNum_PRIVATE_APP) {}

  protected:
    /** For reply module we do all of our processing in the (normally optional)
     * want_replies handling
     */
    virtual MeshPacket *allocReply() override;
};

extern BlackLagerModule *blackLagerModule;
