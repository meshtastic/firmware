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
    BlackLagerModule() : SinglePortModule("black-lager", PortNum_BLACK_LAGER) {}

  protected:
    /** Called to handle a particular incoming message

    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
    it
    */
    virtual ProcessMessage handleReceived(const MeshPacket &mp) override;
};

extern BlackLagerModule *blackLagerModule;
