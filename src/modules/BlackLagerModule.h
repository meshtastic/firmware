#pragma once
#include "Observer.h"
#include "SinglePortModule.h"

/**
 * Text messaging module with public key signed messages.
 */
class BlackLagerModule : public SinglePortModule, public Observable<const MeshPacket *>
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
