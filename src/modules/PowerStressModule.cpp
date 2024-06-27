#include "PowerStressModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

extern void printInfo();

PowerStressModule::PowerStressModule()
    : ProtobufModule("powerstress", meshtastic_PortNum_POWERSTRESS_APP, &meshtastic_PowerStressMessage_msg),
      concurrency::OSThread("PowerStressModule")
{
}

bool PowerStressModule::handleReceivedProtobuf(const meshtastic_MeshPacket &req, meshtastic_PowerStressMessage *pptr)
{
    // We only respond to messages if powermon debugging is already on
    if (config.power.powermon_enables) {
        auto p = *pptr;
        LOG_INFO("Received PowerStress cmd=%d\n", p.cmd);

        // Some commands we can handle immediately, anything else gets deferred to be handled by our thread
        switch (p.cmd) {
        case meshtastic_PowerStressMessage_Opcode_UNSET:
            LOG_ERROR("PowerStress operation unset\n");
            break;

        case meshtastic_PowerStressMessage_Opcode_PRINT_INFO:
            printInfo();
            break;

        default:
            currentMessage = p; // copy for use by thread (the message provided to us will be getting freed)
            break;
        }
    }
    return true;
}

int32_t PowerStressModule::runOnce()
{
    if (config.power.powermon_enables) {
        auto &p = currentMessage;
        switch (p.cmd) {
        case meshtastic_PowerStressMessage_Opcode_UNSET:
            break;
        default:
            LOG_ERROR("PowerStress operation %d not yet implemented!\n", p.cmd);
            break;
        }

        // Done with this command
        p.cmd = meshtastic_PowerStressMessage_Opcode_UNSET;
    } else {
        // Powermon not enabled - stop using CPU
        return disable();
    }

    return 10; // Check for new commands every 10ms
}