#pragma once
#include "Status.h"
#include "configuration.h"
#include "NodeDB.h"
#include <Arduino.h>

extern NodeDB nodeDB;

namespace meshtastic
{

/// Describes the state of the GPS system.
class GPSStatus : public Status
{

  private:
    CallbackObserver<GPSStatus, const GPSStatus *> statusObserver =
        CallbackObserver<GPSStatus, const GPSStatus *>(this, &GPSStatus::updateStatus);

    bool hasLock = false;                // default to false, until we complete our first read
    bool isConnected = false;            // Do we have a GPS we are talking to

    Position p = Position_init_default;

  public:
    GPSStatus() { statusType = STATUS_TYPE_GPS; }

    // proposed for deprecation
    GPSStatus(bool hasLock, bool isConnected, int32_t latitude, int32_t longitude, int32_t altitude, uint32_t dop,
              uint32_t heading, uint32_t numSatellites)
        : Status()
    {
        this->hasLock = hasLock;
        this->isConnected = isConnected;

        this->p.latitude_i = latitude;
        this->p.longitude_i = longitude;
        this->p.altitude = altitude;
        this->p.PDOP = dop;
        this->p.ground_track = heading;
        this->p.sats_in_view = numSatellites;
    }

    // preferred method
    GPSStatus(bool hasLock, bool isConnected, const Position& pos)
        : Status()
    {
        this->hasLock = hasLock;
        this->isConnected = isConnected;

        // all-in-one struct copy
        this->p = pos;
    }

    GPSStatus(const GPSStatus &);
    GPSStatus &operator=(const GPSStatus &);

    void observe(Observable<const GPSStatus *> *source) { statusObserver.observe(source); }

    bool getHasLock() const { return hasLock; }

    bool getIsConnected() const { return isConnected; }

    int32_t getLatitude() const {
        if (radioConfig.preferences.fixed_position){
#ifdef GPS_EXTRAVERBOSE
            DEBUG_MSG("WARNING: Using fixed latitude\n");
#endif
            NodeInfo *node = nodeDB.getNode(nodeDB.getNodeNum());
            return node->position.latitude_i;
        } else {
            return p.latitude_i;
        }
    }

    int32_t getLongitude() const {
        if (radioConfig.preferences.fixed_position){
#ifdef GPS_EXTRAVERBOSE
            DEBUG_MSG("WARNING: Using fixed longitude\n");
#endif
            NodeInfo *node = nodeDB.getNode(nodeDB.getNodeNum());
            return node->position.longitude_i;
        } else {
            return p.longitude_i;
        }
    }

    int32_t getAltitude() const {
        if (radioConfig.preferences.fixed_position){
#ifdef GPS_EXTRAVERBOSE
            DEBUG_MSG("WARNING: Using fixed altitude\n");
#endif
            NodeInfo *node = nodeDB.getNode(nodeDB.getNodeNum());
            return node->position.altitude;
        } else {
            return p.altitude;
        }
    }

    uint32_t getDOP() const { return p.PDOP; }

    uint32_t getHeading() const { return p.ground_track; }

    uint32_t getNumSatellites() const { return p.sats_in_view; }

    bool matches(const GPSStatus *newStatus) const
    {
#ifdef GPS_EXTRAVERBOSE
        DEBUG_MSG("GPSStatus.match() new pos@%x to old pos@%x\n",
                    newStatus->p.pos_timestamp, p.pos_timestamp);
#endif
        return (newStatus->hasLock != hasLock ||
                newStatus->isConnected != isConnected ||
                newStatus->p.latitude_i != p.latitude_i ||
                newStatus->p.longitude_i != p.longitude_i ||
                newStatus->p.altitude != p.altitude ||
                newStatus->p.altitude_hae != p.altitude_hae ||
                newStatus->p.PDOP != p.PDOP ||
                newStatus->p.ground_track != p.ground_track ||
                newStatus->p.sats_in_view != p.sats_in_view);
    }

    int updateStatus(const GPSStatus *newStatus)
    {
        // Only update the status if values have actually changed
        bool isDirty = matches(newStatus);

        if (isDirty && p.pos_timestamp && 
            (newStatus->p.pos_timestamp == p.pos_timestamp)) {
            // We can NEVER be in two locations at the same time! (also PR #886)
            DEBUG_MSG("BUG!! positional timestamp unchanged from prev solution\n");
        }

        initialized = true;
        hasLock = newStatus->hasLock;
        isConnected = newStatus->isConnected;

        p = newStatus->p;

        if (isDirty) {
            if (hasLock) {
                // In debug logs, identify position by @timestamp:stage (stage 3 = notify)
                DEBUG_MSG("New GPS pos@%x:3 lat=%f, lon=%f, alt=%d, pdop=%.2f, track=%.2f, sats=%d\n", 
                            p.pos_timestamp,
                            p.latitude_i * 1e-7, p.longitude_i * 1e-7,
                            p.altitude, p.PDOP * 1e-2, p.ground_track * 1e-5, 
                            p.sats_in_view);
            } else
                DEBUG_MSG("No GPS lock\n");
            onNewStatus.notifyObservers(this);
        }
        return 0;
    }
};

} // namespace meshtastic

extern meshtastic::GPSStatus *gpsStatus;
