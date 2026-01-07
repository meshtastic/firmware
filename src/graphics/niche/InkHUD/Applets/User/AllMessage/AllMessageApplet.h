#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Shows the latest incoming text message, as well as sender.
Both broadcast and direct messages will be shown here, from all channels.

This module doesn't doesn't use the devicestate.rx_text_message,' as this is overwritten to contain outgoing messages
This module doesn't collect its own text message. Instead, the WindowManager stores the most recent incoming text message.
This is available to any interested modules (SingeMessageApplet, NotificationApplet etc.) via InkHUD::latestMessage

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

class AllMessageApplet : public Applet
{
  public:
    void onRender() override;

    void onActivate() override;
    void onDeactivate() override;
    int onReceiveTextMessage(const meshtastic_MeshPacket *p);

    bool approveNotification(Notification &n) override; // Which notifications to suppress

  protected:
    // Used to register our text message callback
    CallbackObserver<AllMessageApplet, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<AllMessageApplet, const meshtastic_MeshPacket *>(this, &AllMessageApplet::onReceiveTextMessage);
};

} // namespace NicheGraphics::InkHUD

#endif