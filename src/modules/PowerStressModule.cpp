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
            if (currentMessage.cmd != meshtastic_PowerStressMessage_Opcode_UNSET)
                LOG_ERROR("PowerStress operation %d already in progress! Can't start new command\n", currentMessage.cmd);
            else
                currentMessage = p; // copy for use by thread (the message provided to us will be getting freed)
            break;
        }
    }
    return true;
}

int32_t PowerStressModule::runOnce()
{

    if (!config.power.powermon_enables) {
        // Powermon not enabled - stop using CPU/stop this thread
        return disable();
    }

    int32_t sleep_msec = 10; // when not active check for new messages every 10ms

    auto &p = currentMessage;

    if (isRunningCommand) {
        // Done with the previous command - our sleep must have finished
        p.cmd = meshtastic_PowerStressMessage_Opcode_UNSET;
        p.num_seconds = 0;
    } else {
        sleep_msec = (int32_t)(p.num_seconds * 1000);
        isRunningCommand = !!sleep_msec; // if the command wants us to sleep, make sure to mark that we have something running

        switch (p.cmd) {
        case meshtastic_PowerStressMessage_Opcode_UNSET: // No need to start a new command
            break;
        case meshtastic_PowerStressMessage_Opcode_LED_ON:
            break;
        default:
            LOG_ERROR("PowerStress operation %d not yet implemented!\n", p.cmd);
            sleep_msec = 0; // Don't do whatever sleep was requested...
            break;
        }
    }
    return sleep_msec;
}