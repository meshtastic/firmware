#pragma once
#include "SinglePortModule.h"
#include "Observer.h"

/**
 * Text message handling for meshtastic - draws on the OLED display the most recent received message
 */
class TextMessageModule : public SinglePortModule, public Observable<const MeshPacket *>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    TextMessageModule() : SinglePortModule("text", PortNum_TEXT_MESSAGE_APP) {}

  protected:

    /** Called to handle a particular incoming message

    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual ProcessMessage handleReceived(const MeshPacket &mp) override;
};

extern TextMessageModule *textMessageModule;
