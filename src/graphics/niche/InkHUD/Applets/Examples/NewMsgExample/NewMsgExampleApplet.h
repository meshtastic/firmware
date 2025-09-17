#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

An example of an InkHUD applet.
Tells us when a new text message arrives.

This applet makes use of the Module API to detect new messages,
which is a general part of the Meshtastic firmware, and not part of InkHUD.

In variants/<your device>/nicheGraphics.h:

    - include this .h file
    - add the following line of code:
        windowManager->addApplet("New Msg", new InkHUD::NewMsgExampleApplet);

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

#include "mesh/SinglePortModule.h"

namespace NicheGraphics::InkHUD
{

class NewMsgExampleApplet : public Applet, public SinglePortModule
{
  public:
    // The MeshModule API requires us to have a constructor, to specify that we're interested in Text Messages.
    NewMsgExampleApplet() : SinglePortModule("NewMsgExampleApplet", meshtastic_PortNum_TEXT_MESSAGE_APP) {}

    // All drawing happens here
    void onRender() override;

    // Your applet might also want to use some of these
    // Useful for setting up or tidying up

    /*
    void onActivate();   // When started
    void onDeactivate(); // When stopped
    void onForeground(); // When shown by short-press
    void onBackground(); // When hidden by short-press
    */

  private:
    // Called when we receive new text messages
    // Part of the MeshModule API
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

    // Store info from handleReceived
    bool haveMessage = false;
    NodeNum fromWho = 0;
};

} // namespace NicheGraphics::InkHUD

#endif