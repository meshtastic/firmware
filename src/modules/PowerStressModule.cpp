#include "PowerStressModule.h"
#include "Led.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerMon.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"
#include "sleep.h"
#include "target_specific.h"
#include <Throttle.h>

extern void printInfo();

PowerStressModule::PowerStressModule()
    : ProtobufModule("powerstress", meshtastic_PortNum_POWERSTRESS_APP, &meshtastic_PowerStressMessage_msg),
      concurrency::OSThread("PowerStress")
{
}

bool PowerStressModule::handleReceivedProtobuf(const meshtastic_MeshPacket &req, meshtastic_PowerStressMessage *pptr)
{
    // We only respond to messages if powermon debugging is already on
    if (config.power.powermon_enables) {
        auto p = *pptr;
        LOG_INFO("Received PowerStress cmd=%d", p.cmd);

        // Some commands we can handle immediately, anything else gets deferred to be handled by our thread
        switch (p.cmd) {
        case meshtastic_PowerStressMessage_Opcode_UNSET:
            LOG_ERROR("PowerStress operation unset");
            break;

        case meshtastic_PowerStressMessage_Opcode_PRINT_INFO:
            printInfo();

            // Now that we know we are actually doing power stress testing, go ahead and turn on all enables (so the log is fully
            // detailed)
            powerMon->force_enabled = true;
            break;

        default:
            if (currentMessage.cmd != meshtastic_PowerStressMessage_Opcode_UNSET)
                LOG_ERROR("PowerStress operation %d already in progress! Can't start new command", currentMessage.cmd);
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
        isRunningCommand = false;
        LOG_INFO("S:PS:%u", p.cmd);
    } else {
        if (p.cmd != meshtastic_PowerStressMessage_Opcode_UNSET) {
            sleep_msec = (int32_t)(p.num_seconds * 1000);
            isRunningCommand = !!sleep_msec; // if the command wants us to sleep, make sure to mark that we have something running
            LOG_INFO(
                "S:PS:%u",
                p.cmd); // Emit a structured log saying we are starting a powerstress state (to make it easier to parse later)

            switch (p.cmd) {
            case meshtastic_PowerStressMessage_Opcode_LED_ON:
                ledForceOn.set(true);
                break;
            case meshtastic_PowerStressMessage_Opcode_LED_OFF:
                ledForceOn.set(false);
                break;
            case meshtastic_PowerStressMessage_Opcode_GPS_ON:
                // FIXME - implement
                break;
            case meshtastic_PowerStressMessage_Opcode_GPS_OFF:
                // FIXME - implement
                break;
            case meshtastic_PowerStressMessage_Opcode_LORA_OFF:
                // FIXME - implement
                break;
            case meshtastic_PowerStressMessage_Opcode_LORA_RX:
                // FIXME - implement
                break;
            case meshtastic_PowerStressMessage_Opcode_LORA_TX:
                // FIXME - implement
                break;
            case meshtastic_PowerStressMessage_Opcode_SCREEN_OFF:
                // FIXME - implement
                break;
            case meshtastic_PowerStressMessage_Opcode_SCREEN_ON:
                // FIXME - implement
                break;
            case meshtastic_PowerStressMessage_Opcode_BT_OFF:
                setBluetoothEnable(false);
                break;
            case meshtastic_PowerStressMessage_Opcode_BT_ON:
                setBluetoothEnable(true);
                break;
            case meshtastic_PowerStressMessage_Opcode_CPU_DEEPSLEEP:
                doDeepSleep(sleep_msec, true, true);
                break;
            case meshtastic_PowerStressMessage_Opcode_CPU_FULLON: {
                uint32_t start_msec = millis();
                while (Throttle::isWithinTimespanMs(start_msec, sleep_msec))
                    ;           // Don't let CPU idle at all
                sleep_msec = 0; // we already slept
                break;
            }
            case meshtastic_PowerStressMessage_Opcode_CPU_IDLE:
                // FIXME - implement
                break;
            default:
                LOG_ERROR("PowerStress operation %d not yet implemented!", p.cmd);
                sleep_msec = 0; // Don't do whatever sleep was requested...
                break;
            }
        }
    }
    return sleep_msec;
}