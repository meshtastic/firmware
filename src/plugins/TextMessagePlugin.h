#pragma once
#include "SinglePortPlugin.h"
#include "Observer.h"

/**
 * Text message handling for meshtastic - draws on the OLED display the most recent received message
 */
class TextMessagePlugin : public SinglePortPlugin, public Observable<const MeshPacket *>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    TextMessagePlugin() : SinglePortPlugin("text", PortNum_TEXT_MESSAGE_APP) {}

  protected:

    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceived(const MeshPacket &mp);
};

extern TextMessagePlugin textMessagePlugin;