#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

A notification which might be displayed by the NotificationApplet

An instance of this class is offered to Applets via Applet::approveNotification, in case they want to veto the notification.
An Applet should veto a notification if it is already displaying the same info which the notification would convey.

*/

#pragma once

#include "configuration.h"

namespace NicheGraphics::InkHUD
{

class Notification
{
  public:
    enum Type : uint8_t {
        NOTIFICATION_MESSAGE_BROADCAST,
        NOTIFICATION_MESSAGE_DIRECT,
        NOTIFICATION_BATTERY,
        NOTIFICATION_GEOFENCE
    } type;

    uint32_t timestamp;

    uint8_t getChannel() { return channel; }
    uint32_t getSender() { return sender; }
    uint8_t getBatteryPercentage() { return batteryPercentage; }

    friend class NotificationApplet;

  protected:
    uint8_t channel;
    uint32_t sender;
    uint8_t batteryPercentage;
    char geofenceNodeName[sizeof(meshtastic_User::long_name)];
    char geofenceName[sizeof(meshtastic_Waypoint::name)];
    uint32_t geofenceWaypointId;
    bool geofenceEntered;
};

} // namespace NicheGraphics::InkHUD

#endif
