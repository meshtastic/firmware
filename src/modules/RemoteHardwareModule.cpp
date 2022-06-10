#include "configuration.h"
#include "RemoteHardwareModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "main.h"

#define NUM_GPIOS 64

// Because (FIXME) we currently don't tell API clients status on sent messages
// we need to throttle our sending, so that if a gpio is bouncing up and down we
// don't generate more messages than the net can send. So we limit watch messages to
// a max of one change per 30 seconds
#define WATCH_INTERVAL_MSEC (30 * 1000)

/// Set pin modes for every set bit in a mask
static void pinModes(uint64_t mask, uint8_t mode)
{
    for (uint64_t i = 0; i < NUM_GPIOS; i++) {
        if (mask & (1 << i)) {
            pinMode(i, mode);
        }
    }
}

/// Read all the pins mentioned in a mask
static uint64_t digitalReads(uint64_t mask)
{
    uint64_t res = 0;

    // The Arduino docs show to run pinMode(). But, when testing, found it is best not to.
    // If the line below is uncommented, read will flip the pin to the default of the second
    // argument in pinModes(), which will make the read turn the PIN "on".
    //pinModes(mask, INPUT_PULLUP);

    for (uint64_t i = 0; i < NUM_GPIOS; i++) {
        uint64_t m = 1 << i;
        if (mask & m) {
            if (digitalRead(i)) {
                res |= m;
            }
        }
    }

    return res;
}

RemoteHardwareModule::RemoteHardwareModule()
    : ProtobufModule("remotehardware", PortNum_REMOTE_HARDWARE_APP, HardwareMessage_fields), concurrency::OSThread(
                                                                                                 "remotehardware")
{
}

bool RemoteHardwareModule::handleReceivedProtobuf(const MeshPacket &req, HardwareMessage *pptr)
{
    auto p = *pptr;
    DEBUG_MSG("Received RemoteHardware typ=%d\n", p.typ);

    switch (p.typ) {
    case HardwareMessage_Type_WRITE_GPIOS:
        // Print notification to LCD screen
        screen->print("Write GPIOs\n");

        for (uint8_t i = 0; i < NUM_GPIOS; i++) {
            uint64_t mask = 1 << i;
            if (p.gpio_mask & mask) {
                digitalWrite(i, (p.gpio_value & mask) ? 1 : 0);
            }
        }
        pinModes(p.gpio_mask, OUTPUT);

        break;

    case HardwareMessage_Type_READ_GPIOS: {
        // Print notification to LCD screen
        if (screen)
            screen->print("Read GPIOs\n");

        uint64_t res = digitalReads(p.gpio_mask);

        // Send the reply
        HardwareMessage r = HardwareMessage_init_default;
        r.typ = HardwareMessage_Type_READ_GPIOS_REPLY;
        r.gpio_value = res;
        r.gpio_mask = p.gpio_mask;
        MeshPacket *p2 = allocDataProtobuf(r);
        setReplyTo(p2, req);
        myReply = p2;
        break;
    }

    case HardwareMessage_Type_WATCH_GPIOS: {
        watchGpios = p.gpio_mask;
        lastWatchMsec = 0;           // Force a new publish soon
        previousWatch = ~watchGpios; // generate a 'previous' value which is guaranteed to not match (to force an initial publish)
        enabled = true;              // Let our thread run at least once
        DEBUG_MSG("Now watching GPIOs 0x%llx\n", watchGpios);
        break;
    }

    case HardwareMessage_Type_READ_GPIOS_REPLY:
    case HardwareMessage_Type_GPIOS_CHANGED:
        break; // Ignore - we might see our own replies

    default:
        DEBUG_MSG("Hardware operation %d not yet implemented! FIXME\n", p.typ);
        break;
    }

    return false;
}

int32_t RemoteHardwareModule::runOnce()
{
    if (watchGpios) {
        uint32_t now = millis();

        if (now - lastWatchMsec >= WATCH_INTERVAL_MSEC) {
            uint64_t curVal = digitalReads(watchGpios);

            if (curVal != previousWatch) {
                previousWatch = curVal;
                DEBUG_MSG("Broadcasting GPIOS 0x%llx changed!\n", curVal);

                // Something changed!  Tell the world with a broadcast message
                HardwareMessage r = HardwareMessage_init_default;
                r.typ = HardwareMessage_Type_GPIOS_CHANGED;
                r.gpio_value = curVal;
                MeshPacket *p = allocDataProtobuf(r);
                service.sendToMesh(p);
            }
        }
    } else {
        // No longer watching anything - stop using CPU
        enabled = false;
    }

    return 200; // Poll our GPIOs every 200ms (FIXME, make adjustable via protobuf arg)
}
