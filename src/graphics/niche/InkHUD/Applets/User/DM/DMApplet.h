#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Shows the latest incoming *Direct Message* (DM), as well as sender.
This complements the threaded message applets

This module doesn't collect its own text message. Instead, the WindowManager stores the most recent incoming text message.
This is available to any interested modules (SingleMessageApplet, NotificationApplet etc.) via InkHUD::latestMessage

We do still receive notifications from the text message module though,
to know when a new message has arrived, and trigger the update.

*/

#pragma once

#include "configuration.h"

#include "graphics/niche/InkHUD/Applet.h"

#include "modules/TextMessageModule.h"

namespace NicheGraphics::InkHUD
{

class Applet;

class DMApplet : public Applet
{
  public:
    void onRender(bool full) override;

    void onActivate() override;
    void onDeactivate() override;
    int onReceiveTextMessage(const meshtastic_MeshPacket *p);
    void onNavUp() override;   // Scroll a long message up
    void onNavDown() override; // Scroll a long message down

    bool approveNotification(Notification &n) override; // Which notifications to suppress

  protected:
    void scrollBy(int direction);

    uint16_t scrollY = 0;    // Pixels of the message text scrolled off the top
    uint16_t maxScrollY = 0; // Set by onRender: how far the text can scroll
    uint16_t scrollStep = 0; // Set by onRender: one line of the font in use

    // Used to register our text message callback
    CallbackObserver<DMApplet, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<DMApplet, const meshtastic_MeshPacket *>(this, &DMApplet::onReceiveTextMessage);
};

} // namespace NicheGraphics::InkHUD

#endif
