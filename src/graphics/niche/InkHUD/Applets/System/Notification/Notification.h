#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

A notification which might be displayed by the NotificationApplet

An instance of this class is offered to Applets via Applet::approveNotification, in case they want to veto the notification.
An Applet should veto a notification if it is already displaying the same info which the notification would convey.

*/

#pragma once

#include "configuration.h"
#include <cstdint>
#include <cstring>

namespace NicheGraphics::InkHUD
{

class Notification
{
  public:
    enum Type : uint8_t {
        NOTIFICATION_MESSAGE_BROADCAST,
        NOTIFICATION_MESSAGE_DIRECT,
        NOTIFICATION_GEOFENCE
    } type = NOTIFICATION_MESSAGE_BROADCAST;

    uint32_t timestamp = 0;

    uint8_t getChannel() const { return channel; }
    uint32_t getSender() const { return sender; }
    const char *getGeofenceNodeName() const { return geofenceNodeName; }
    const char *getGeofenceName() const { return geofenceName; }
    bool getGeofenceEntered() const { return geofenceEntered; }
    void setGeofenceNodeName(const char *name)
    {
        if (!name) {
            geofenceNodeName[0] = '\0';
            return;
        }
        strncpy(geofenceNodeName, name, sizeof(geofenceNodeName) - 1);
        geofenceNodeName[sizeof(geofenceNodeName) - 1] = '\0';
    }
    void setGeofenceName(const char *name)
    {
        if (!name) {
            geofenceName[0] = '\0';
            return;
        }
        strncpy(geofenceName, name, sizeof(geofenceName) - 1);
        geofenceName[sizeof(geofenceName) - 1] = '\0';
    }
    void setGeofenceEntered(bool entered) { geofenceEntered = entered; }

    friend class NotificationApplet;

  protected:
    uint8_t channel = 0;
    uint32_t sender = 0;
    char geofenceNodeName[40] = {};
    char geofenceName[40] = {};
    bool geofenceEntered = false;
};

} // namespace NicheGraphics::InkHUD

#endif
