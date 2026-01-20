#pragma once
#if !MESHTASTIC_EXCLUDE_STATUS
#include "SinglePortModule.h"
#include "configuration.h"

class StatusMessageModule : public SinglePortModule, private concurrency::OSThread
{

  public:
    /** Constructor
     * name is for debugging output
     */
    StatusMessageModule()
        : SinglePortModule("statusMessage", meshtastic_PortNum_NODE_STATUS_APP), concurrency::OSThread("StatusMessage")
    {
        if (moduleConfig.has_statusmessage && moduleConfig.statusmessage.node_status[0] != '\0') {
            this->setInterval(2 * 60 * 1000);
        }
        // TODO: If we have a string, set the initial delay (15 minutes maybe)
    }

    virtual int32_t runOnce() override;

  protected:
    /** Called to handle a particular incoming message
     */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  private:
};

extern StatusMessageModule *statusMessageModule;
#endif