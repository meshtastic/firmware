#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Pop-up notification bar, on screen top edge
Displays information we feel is important, but which is not shown on currently focused applet(s)
E.g.: messages, while viewing map, etc

Feature should be optional; enable disable via on-screen menu

*/

#pragma once

#include "configuration.h"

#include "concurrency/OSThread.h"

#include "graphics/niche/InkHUD/SystemApplet.h"

namespace NicheGraphics::InkHUD
{

class NotificationApplet : public SystemApplet
{
  public:
    NotificationApplet();

    void onRender() override;
    void onForeground() override;
    void onBackground() override;
    void onButtonShortPress() override;
    void onButtonLongPress() override;

    int onReceiveTextMessage(const meshtastic_MeshPacket *p);

    bool isApproved(); // Does a foreground applet make notification redundant?
    void dismiss();    // Close the Notification Popup

  protected:
    // Get notified when a new text message arrives
    CallbackObserver<NotificationApplet, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<NotificationApplet, const meshtastic_MeshPacket *>(this, &NotificationApplet::onReceiveTextMessage);

    std::string getNotificationText(uint16_t widthAvailable); // Get text for notification, to suit screen width

    bool hasNotification = false;                      // Only used for assert. Todo: remove?
    Notification currentNotification = Notification(); // Set when something notification-worthy happens. Used by render()
};

} // namespace NicheGraphics::InkHUD

#endif