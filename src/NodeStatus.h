#pragma once
#include <Arduino.h>
#include "Status.h"
#include "configuration.h"

namespace meshtastic {

    /// Describes the state of the NodeDB system.
    class NodeStatus : public Status
    {

       private:
        CallbackObserver<NodeStatus, const NodeStatus *> statusObserver = CallbackObserver<NodeStatus, const NodeStatus *>(this, &NodeStatus::updateStatus);

        uint8_t numOnline = 0;
        uint8_t numTotal = 0;

       public:

        NodeStatus() {
            statusType = STATUS_TYPE_NODE;
        }
        NodeStatus( uint8_t numOnline, uint8_t numTotal ) : Status()
        {
            this->numOnline = numOnline;
            this->numTotal = numTotal;
        }
        NodeStatus(const NodeStatus &);
        NodeStatus &operator=(const NodeStatus &);

        void observe(Observable<const NodeStatus *> *source)
        {
            statusObserver.observe(source);
        }

        uint8_t getNumOnline() const
        { 
            return numOnline; 
        }

        uint8_t getNumTotal() const
        { 
            return numTotal; 
        }

        bool matches(const NodeStatus *newStatus) const
        {
            return (
                newStatus->getNumOnline() != numOnline ||
                newStatus->getNumTotal() != numTotal
            );
        }
        int updateStatus(const NodeStatus *newStatus) {
            // Only update the status if values have actually changed
            bool isDirty;
            {
                isDirty = matches(newStatus);
                initialized = true; 
                numOnline = newStatus->getNumOnline();
                numTotal = newStatus->getNumTotal();
            }
            if(isDirty) {
                DEBUG_MSG("Node status update: %d online, %d total\n", numOnline, numTotal);            
                onNewStatus.notifyObservers(this);
            }
            return 0;
        }

    };

}

extern meshtastic::NodeStatus *nodeStatus;