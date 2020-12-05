#pragma once
#include "MeshPlugin.h"
#include "Observer.h"

/**
 * Text message handling for meshtastic - draws on the OLED display the most recent received message
 */
class TextMessagePlugin : public MeshPlugin, public Observable<const MeshPacket *>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    TextMessagePlugin() : MeshPlugin("text") {}

  protected:
    /**
     * @return true if you want to receive the specified portnum
     */
    virtual bool wantPortnum(PortNum p) { return p == PortNum_TEXT_MESSAGE_APP; }

    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceived(const MeshPacket &mp);
};

extern TextMessagePlugin textMessagePlugin;