#pragma once
#include "Status.h"
#include "configuration.h"
#include <Arduino.h>

namespace meshtastic
{

/// Describes the state of the NodeDB system.
class NodeStatus : public Status
{

  private:
    CallbackObserver<NodeStatus, const NodeStatus *> statusObserver =
        CallbackObserver<NodeStatus, const NodeStatus *>(this, &NodeStatus::updateStatus);

    uint16_t numOnline = 0;
    uint16_t numTotal = 0;

    uint16_t lastNumTotal = 0;

  public:
    bool forceUpdate = false;

    NodeStatus() { statusType = STATUS_TYPE_NODE; }
    NodeStatus(uint16_t numOnline, uint16_t numTotal, bool forceUpdate = false) : Status()
    {
        this->forceUpdate = forceUpdate;
        this->numOnline = numOnline;
        this->numTotal = numTotal;
    }
    NodeStatus(const NodeStatus &);
    NodeStatus &operator=(const NodeStatus &);

    void observe(Observable<const NodeStatus *> *source) { statusObserver.observe(source); }

    uint16_t getNumOnline() const { return numOnline; }

    uint16_t getNumTotal() const { return numTotal; }

    uint16_t getLastNumTotal() const { return lastNumTotal; }

    bool matches(const NodeStatus *newStatus) const
    {
        return (newStatus->getNumOnline() != numOnline || newStatus->getNumTotal() != numTotal);
    }
    int updateStatus(const NodeStatus *newStatus)
    {
        // Only update the status if values have actually changed
        lastNumTotal = numTotal;
        bool isDirty;
        {
            isDirty = matches(newStatus);
            initialized = true;
            numOnline = newStatus->getNumOnline();
            numTotal = newStatus->getNumTotal();
        }
        if (isDirty || newStatus->forceUpdate) {
            LOG_DEBUG("Node status update: %u online, %u total", numOnline, numTotal);
            onNewStatus.notifyObservers(this);
        }
        return 0;
    }
};

} // namespace meshtastic

extern meshtastic::NodeStatus *nodeStatus;