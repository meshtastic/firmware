#pragma once
#include "Status.h"
#include "configuration.h"
#include <Arduino.h>

namespace meshtastic
{

/// Describes the state of the GPS system.
class MagnotometerStatus : public Status
{

  private:
    CallbackObserver<MagnotometerStatus, const MagnotometerStatus *> statusObserver =
        CallbackObserver<MagnotometerStatus, const MagnotometerStatus *>(this, &MagnotometerStatus::updateStatus);

    bool isConnected = false; // Do we have a GPS we are talking to

    bool isPowerSaving = false; // Are we in power saving state

    float heading = 0.0; // Heading in degrees
  public:
    MagnotometerStatus() { statusType = STATUS_TYPE_MAGNOTOMETER; }

    // preferred method
    MagnotometerStatus(bool isConnected, bool isPowerSaving, int heading) : Status()
    {
        this->isConnected = isConnected;
        this->isPowerSaving = isPowerSaving;
        this->heading = heading;
    }

    MagnotometerStatus(const MagnotometerStatus &);
    MagnotometerStatus &operator=(const MagnotometerStatus &);

    void observe(Observable<const MagnotometerStatus *> *source) { statusObserver.observe(source); }

    bool getIsConnected() const { return isConnected; }

    bool getIsPowerSaving() const { return isPowerSaving; }

    float getHeading() const
    {
        return heading;
    }

    bool matches(const MagnotometerStatus *newStatus) const
    {
#ifdef MAG_EXTRAVERBOSE
        LOG_DEBUG("MagStatus.match() new heading = @%d to old heading@%d\n", newStatus->heading, heading);
#endif
        return ( newStatus->isConnected != isConnected ||
                newStatus->isPowerSaving != isPowerSaving ||
                newStatus->heading != heading);
    }

    int updateStatus(const MagnotometerStatus *newStatus)
    {
        // Only update the status if values have actually changed
        bool isDirty = matches(newStatus);

        initialized = true;
        isConnected = newStatus->isConnected;
        heading = newStatus->heading;
        if (isDirty) {
            // In debug logs, identify position by @timestamp:stage (stage 3 = notify)
            LOG_DEBUG("New heading %f\n", heading);
            onNewStatus.notifyObservers(this);
        }
        return 0;
    }
};

} // namespace meshtastic

extern meshtastic::MagnotometerStatus *magnotometerStatus;