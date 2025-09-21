#pragma once
#include "Observer.h"
#include "SinglePortModule.h"
#include <string>

/**
 * Text message handling for meshtastic - draws on the OLED display the most recent received message
 */
class TextMessageModule : public SinglePortModule, public Observable<const meshtastic_MeshPacket *>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    TextMessageModule() : SinglePortModule("text", meshtastic_PortNum_TEXT_MESSAGE_APP) {}

    bool sendText(uint32_t to, uint8_t channel, const std::string &text); // send a text message to a particular node

  protected:
    /** Called to handle a particular incoming message

    @return ProcessMessage::STOP if you've guaranteed you've handled this message and no other handlers should be considered for
    it
    */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
};

extern TextMessageModule *textMessageModule;