#pragma once
#include "Observer.h"
#include "SinglePortModule.h"

/**
 * Text message handling for Meshtastic.
 *
 * This module is responsible for receiving and storing incoming text messages
 * from the mesh. It updates device state and notifies observers so that other
 * components (such as the MessageRenderer) can later display or process them.
 *
 * Rendering of messages on screen is no longer done here.
 */
class TextMessageModule : public SinglePortModule, public Observable<const meshtastic_MeshPacket *>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    TextMessageModule() : SinglePortModule("text", meshtastic_PortNum_TEXT_MESSAGE_APP) {}

  protected:
    /** Called to handle a particular incoming message
     *
     * @return ProcessMessage::STOP if you've guaranteed you've handled this
     *         message and no other handlers should be considered for it.
     */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
};

extern TextMessageModule *textMessageModule;
