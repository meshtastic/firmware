#pragma once
#include <Arduino.h>
#include "lock.h"
#include "configuration.h"

namespace meshtastic {

/// Describes the state of the GPS system.
struct NodeStatus 
{

    bool isDirty = false;
    size_t numOnline; // Number of nodes online
    size_t numTotal;  // Number of nodes total

};

class NodeStatusHandler 
{

   private:
    NodeStatus status;
    CallbackObserver<NodeStatusHandler, const NodeStatus> nodeObserver = CallbackObserver<NodeStatusHandler, const NodeStatus>(this, &NodeStatusHandler::updateStatus);
    bool initialized = false;
    /// Protects all of internal state.
    Lock lock;

   public:
    Observable<void *> onNewStatus;

    void observe(Observable<const NodeStatus> *source)
    {
        nodeObserver.observe(source);
    }

    bool isInitialized() const { return initialized; }
    size_t getNumOnline() const { return status.numOnline; }
    size_t getNumTotal() const { return status.numTotal; }

    int updateStatus(const NodeStatus newStatus) 
    {
        // Only update the status if values have actually changed
        status.isDirty = (
            newStatus.numOnline != status.numOnline ||
            newStatus.numTotal != status.numTotal
        );
        LockGuard guard(&lock);
        initialized = true; 
        status.numOnline = newStatus.numOnline;
        status.numTotal = newStatus.numTotal;
        if(status.isDirty) {
            DEBUG_MSG("Node status update: %d online, %d total\n", status.numOnline, status.numTotal);            
            onNewStatus.notifyObservers(NULL);
        }
        return 0;
    }

};

}

extern meshtastic::NodeStatus nodeStatus;