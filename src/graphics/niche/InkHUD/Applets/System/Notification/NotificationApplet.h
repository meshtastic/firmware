#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

Pop-up notification bar, on screen top edge
Displays information we feel is important, but which is not shown on currently focused applet(s)
E.g.: messages, while viewing map, etc

Feature should be optional; enable disable via on-screen menu

*/

#pragma once

#include "configuration.h"

#include <array>

#include "graphics/niche/InkHUD/SystemApplet.h"
#if !MESHTASTIC_EXCLUDE_WAYPOINT
struct GeofenceNotificationEvent;
#endif

namespace NicheGraphics::InkHUD
{

class NotificationApplet : public SystemApplet
{
  public:
    static constexpr uint16_t DEFAULT_HEIGHT = 20;
    static constexpr uint8_t MAX_WRAPPED_LINES = 2;

    NotificationApplet();

    void onRender(bool full) override;
    void onForeground() override;
    void onBackground() override;
    void onButtonShortPress() override;
    void onButtonLongPress() override;
    void onExitShort() override;
    void onExitLong() override;
    void onNavUp() override;
    void onNavDown() override;
    void onNavLeft() override;
    void onNavRight() override;

    int onReceiveTextMessage(const meshtastic_MeshPacket *p);
#if !MESHTASTIC_EXCLUDE_WAYPOINT
    int onGeofenceEvent(const GeofenceNotificationEvent *event);
#endif

    bool isApproved(); // Does a foreground applet make notification redundant?
    void dismiss();    // Close the Notification Popup

  protected:
    struct PreparedLine {
        std::string text;
        uint16_t width = 0;
    };

    // Get notified when a new text message arrives
    CallbackObserver<NotificationApplet, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<NotificationApplet, const meshtastic_MeshPacket *>(this, &NotificationApplet::onReceiveTextMessage);
#if !MESHTASTIC_EXCLUDE_WAYPOINT
    CallbackObserver<NotificationApplet, const GeofenceNotificationEvent *> geofenceObserver =
        CallbackObserver<NotificationApplet, const GeofenceNotificationEvent *>(this, &NotificationApplet::onGeofenceEvent);
#endif

    void showNotification(const Notification &n);
    void clearPreparedLines();
    void resetTileHeight();
    void prepareCurrentNotificationLayout();
    std::string getNotificationText(uint16_t widthAvailable); // Get text for notification, to suit screen width

    bool hasNotification = false;                       // Only used for assert. Todo: remove?
    bool preparedWrapped = false;
    uint8_t preparedLineCount = 0;
    uint16_t preparedTextHeight = 0;
    std::array<PreparedLine, MAX_WRAPPED_LINES> preparedLines = {};
    Notification currentNotification = Notification();  // Set when something notification-worthy happens. Used by render()
};

} // namespace NicheGraphics::InkHUD

#endif
