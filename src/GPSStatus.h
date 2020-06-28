#pragma once
#include <Arduino.h>
#include "lock.h"
#include "configuration.h"

namespace meshtastic {

/// Describes the state of the GPS system.
struct GPSStatus 
{

    bool isDirty = false;
    bool hasLock = false; // default to false, until we complete our first read
    bool isConnected = false; // Do we have a GPS we are talking to

    int32_t latitude = 0, longitude = 0; // as an int mult by 1e-7 to get value as double
    int32_t altitude = 0;
    uint32_t dop = 0; // Diminution of position; PDOP where possible (UBlox), HDOP otherwise (TinyGPS) in 10^2 units (needs scaling before use)

};

class GPSStatusHandler 
{

   private:
    GPSStatus status;
    CallbackObserver<GPSStatusHandler, const GPSStatus> gpsObserver = CallbackObserver<GPSStatusHandler, const GPSStatus>(this, &GPSStatusHandler::updateStatus);
    bool initialized = false;
    /// Protects all of internal state.
    Lock lock;

   public:
    Observable<void *> onNewStatus;

    void observe(Observable<const GPSStatus> *source)
    {
        gpsObserver.observe(source);
    }

    bool isInitialized() { LockGuard guard(&lock); return initialized; }
    bool hasLock() { LockGuard guard(&lock); return status.hasLock; }
    bool isConnected() { LockGuard guard(&lock); return status.isConnected; }
    int32_t getLatitude() { LockGuard guard(&lock); return status.latitude; }
    int32_t getLongitude() { LockGuard guard(&lock); return status.longitude; }
    int32_t getAltitude() { LockGuard guard(&lock); return status.altitude; }
    uint32_t getDOP() { LockGuard guard(&lock); return status.dop; }

    int updateStatus(const GPSStatus newStatus) {
        // Only update the status if values have actually changed
        status.isDirty = (
            newStatus.hasLock != status.hasLock ||
            newStatus.isConnected != status.isConnected ||
            newStatus.latitude != status.latitude ||
            newStatus.longitude != status.longitude ||
            newStatus.altitude != status.altitude ||
            newStatus.dop != status.latitude
        );
        {
            LockGuard guard(&lock);
            initialized = true; 
            status.hasLock = newStatus.hasLock;
            status.isConnected = newStatus.isConnected;
            status.latitude = newStatus.latitude;
            status.longitude = newStatus.longitude;
            status.altitude = newStatus.altitude;
            status.dop = newStatus.dop;
        }
        if(status.isDirty) {
            DEBUG_MSG("New GPS pos lat=%f, lon=%f, alt=%d, pdop=%f\n", status.latitude * 1e-7, status.longitude * 1e-7, status.altitude, status.dop * 1e-2);            
            onNewStatus.notifyObservers(NULL);
        }
        return 0;
    }

};

}

extern meshtastic::GPSStatus gpsStatus;