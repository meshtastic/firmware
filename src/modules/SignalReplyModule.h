#pragma once
#include "SinglePortModule.h"

/**
 * A simple example module that just replies with "Message received" to any message it receives.
 */
class SignalReplyModule : public SinglePortModule, public Observable<const meshtastic_MeshPacket *>
{
  public:
    /** Constructor
     * name is for debugging output
     */
    SignalReplyModule() : SinglePortModule("XXXXMod", meshtastic_PortNum_TEXT_MESSAGE_APP) {}

  //virtual ~SignalReplyModule() {}

  protected:
    /** For reply module we do all of our processing in the (normally optional)
     * want_replies handling
     */

    virtual meshtastic_MeshPacket *allocReply() override;
    virtual bool wantPacket(const meshtastic_MeshPacket *p) override;
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;


};

extern SignalReplyModule *signalReplyModule;
