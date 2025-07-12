#include "SeesawRotary.h"
#include "meshUtils.h"

#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_GPS
#include "GPS.h"
#endif
#include "MeshService.h"
#include "RadioLibInterface.h"
#include "buzz.h"
#include "input/InputBroker.h"
#include "main.h"
#include "modules/CannedMessageModule.h"
#include "modules/ExternalNotificationModule.h"
#include "power.h"
#include "sleep.h"
#ifdef ARCH_PORTDUINO
#include "platform/portduino/PortduinoGlue.h"
#endif

using namespace concurrency;

SeesawRotary *seesawRotary;

SeesawRotary::SeesawRotary(const char *name) : OSThread(name)
{
    _originName = name;
}

bool SeesawRotary::init()
{
    if (inputBroker)
        inputBroker->registerSource(this);

    if (!ss.begin(SEESAW_ADDR)) {
        return false;
    }
    // attachButtonInterrupts();

    uint32_t version = ((ss.getVersion() >> 16) & 0xFFFF);
    if (version != 4991) {
        LOG_WARN("Wrong firmware loaded? %u", version);
    } else {
        LOG_INFO("Found Product 4991");
    }
    /*
    #ifdef ARCH_ESP32
        // Register callbacks for before and after lightsleep
        // Used to detach and reattach interrupts
        lsObserver.observe(&notifyLightSleep);
        lsEndObserver.observe(&notifyLightSleepEnd);
    #endif
    */
    ss.pinMode(SS_SWITCH, INPUT_PULLUP);

    // get starting position
    encoder_position = ss.getEncoderPosition();

    LOG_INFO("Turning on interrupts");
    ss.setGPIOInterrupts((uint32_t)1 << SS_SWITCH, 1);
    ss.enableEncoderInterrupt();
    canSleep = true; // Assume we should not keep the board awake

    return true;
}

int32_t SeesawRotary::runOnce()
{
    if (!ss.digitalRead(SS_SWITCH)) {
        LOG_WARN("Button pressed!");
    }
    int32_t new_position = ss.getEncoderPosition();
    // did we move arounde?
    if (encoder_position != new_position) {
        LOG_WARN("Old position: %u, New Position: %u", encoder_position, new_position);
        encoder_position = new_position;
    }

    return 50;
}
