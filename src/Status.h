#pragma once

#include "Observer.h"

// Constants for the various status types, so we can tell subclass instances apart
#define STATUS_TYPE_BASE 0
#define STATUS_TYPE_POWER 1
#define STATUS_TYPE_GPS 2
#define STATUS_TYPE_NODE 3

namespace meshtastic
{

// A base class for observable status
class Status
{
  protected:
    // Allows us to observe an Observable
    CallbackObserver<Status, const Status *> statusObserver =
        CallbackObserver<Status, const Status *>(this, &Status::updateStatus);
    bool initialized = false;
    // Workaround for no typeid support
    int statusType = 0;

  public:
    // Allows us to generate observable events
    Observable<const Status *> onNewStatus;

    // Enable polymorphism ?
    virtual ~Status() = default;

    Status()
    {
        if (!statusType) {
            statusType = STATUS_TYPE_BASE;
        }
    }

    // Prevent object copy/move
    Status(const Status &) = delete;
    Status &operator=(const Status &) = delete;

    // Start observing a source of data
    void observe(Observable<const Status *> *source) { statusObserver.observe(source); }

    // Determines whether or not existing data matches the data in another Status instance
    bool matches(const Status *otherStatus) const { return true; }

    bool isInitialized() const { return initialized; }

    int getStatusType() const { return statusType; }

    // Called when the Observable we're observing generates a new notification
    int updateStatus(const Status *newStatus) { return 0; }
};
}; // namespace meshtastic
