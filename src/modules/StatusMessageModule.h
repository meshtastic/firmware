#pragma once
#if !MESHTASTIC_EXCLUDE_STATUS
#include "SinglePortModule.h"
#include "configuration.h"
#include <string>
#include <vector>

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
        } else {
            this->setInterval(1000 * 12 * 60 * 60);
        }
        // TODO: If we have a string, set the initial delay (15 minutes maybe)

        // Keep vector from reallocating as we fill up to MAX_RECENT_STATUSMESSAGES
        recentReceived.reserve(MAX_RECENT_STATUSMESSAGES);
    }

    virtual int32_t runOnce() override;

    struct RecentStatus {
        uint32_t fromNodeId;    // mp.from
        std::string statusText; // incomingMessage.status
    };

    const std::vector<RecentStatus> &getRecentReceived() const { return recentReceived; }

  protected:
    /** Called to handle a particular incoming message
     */
    virtual ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override;

  private:
    static constexpr size_t MAX_RECENT_STATUSMESSAGES = 5;
    std::vector<RecentStatus> recentReceived;
};

extern StatusMessageModule *statusMessageModule;
#endif