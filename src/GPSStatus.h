#pragma once
#include <Arduino.h>
#include "Status.h"
#include "configuration.h"

namespace meshtastic {

    /// Describes the state of the GPS system.
    class GPSStatus : public Status
    {

       private:
        CallbackObserver<GPSStatus, const GPSStatus *> statusObserver = CallbackObserver<GPSStatus, const GPSStatus *>(this, &GPSStatus::updateStatus);

        bool hasLock = false; // default to false, until we complete our first read
        bool isConnected = false; // Do we have a GPS we are talking to
        int32_t latitude = 0, longitude = 0; // as an int mult by 1e-7 to get value as double
        int32_t altitude = 0;
        uint32_t dop = 0; // Diminution of position; PDOP where possible (UBlox), HDOP otherwise (TinyGPS) in 10^2 units (needs scaling before use)

       public:

        GPSStatus() {
            statusType = STATUS_TYPE_GPS;
        }
        GPSStatus( bool hasLock, bool isConnected, int32_t latitude, int32_t longitude, int32_t altitude, uint32_t dop ) : Status()
        {
            this->hasLock = hasLock;
            this->isConnected = isConnected;
            this->latitude = latitude;
            this->longitude = longitude;
            this->altitude = altitude;
            this->dop = dop;
        }
        GPSStatus(const GPSStatus &);
        GPSStatus &operator=(const GPSStatus &);

        void observe(Observable<const GPSStatus *> *source)
        {
            statusObserver.observe(source);
        }

        bool getHasLock() const
        { 
            return hasLock; 
        }

        bool getIsConnected() const
        { 
            return isConnected; 
        }

        int32_t getLatitude() const
        { 
            return latitude; 
        }

        int32_t getLongitude() const
        { 
            return longitude;
        }

        int32_t getAltitude() const
        { 
            return altitude; 
        }

        uint32_t getDOP() const
        { 
            return dop; 
        }

        bool matches(const GPSStatus *newStatus) const
        {
            return (
                newStatus->hasLock != hasLock ||
                newStatus->isConnected != isConnected ||
                newStatus->latitude != latitude ||
                newStatus->longitude != longitude ||
                newStatus->altitude != altitude ||
                newStatus->dop != dop
            );
        }
        int updateStatus(const GPSStatus *newStatus) {
            // Only update the status if values have actually changed
            bool isDirty;
            {
                isDirty = matches(newStatus);
                initialized = true; 
                hasLock = newStatus->hasLock;
                isConnected = newStatus->isConnected;
                latitude = newStatus->latitude;
                longitude = newStatus->longitude;
                altitude = newStatus->altitude;
                dop = newStatus->dop;
            }
            if(isDirty) {
                DEBUG_MSG("New GPS pos lat=%f, lon=%f, alt=%d, pdop=%f\n", latitude * 1e-7, longitude * 1e-7, altitude, dop * 1e-2);            
                onNewStatus.notifyObservers(this);
            }
            return 0;
        }

    };

}

extern meshtastic::GPSStatus *gpsStatus;