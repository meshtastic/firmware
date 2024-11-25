#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Shows the latest incoming text message, as well as sender

This module doesn't doesn't use the devicestate.rx_text_message,' as this is overwritten to contain outgoing messages
This module doesn't collect its own text message. Instead, the WindowManager stores the most recent incoming text message.
This is available to any interested modules (SingeMessageApplet, NotificationApplet etc.) via settings.lastMessage

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

class SingleMessageApplet : public Applet
{
  public:
    void render() override;

    bool approveNotification(Notification &n) override;

    void onActivate() override;
    void onDeactivate() override;

    int onReceiveTextMessage(const meshtastic_MeshPacket *p);

  protected:
    CallbackObserver<SingleMessageApplet, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<SingleMessageApplet, const meshtastic_MeshPacket *>(this, &SingleMessageApplet::onReceiveTextMessage);
};

} // namespace NicheGraphics::InkHUD

#endif