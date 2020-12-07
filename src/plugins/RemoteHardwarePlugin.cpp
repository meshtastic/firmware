#include "RemoteHardwarePlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

RemoteHardwarePlugin remoteHardwarePlugin;

#define NUM_GPIOS 64

// A macro for clearing a struct, FIXME, move elsewhere
#define CLEAR_STRUCT(r) memset(&r, 0, sizeof(r))

bool RemoteHardwarePlugin::handleReceivedProtobuf(const MeshPacket &req, const HardwareMessage &p)
{
    switch (p.typ) {
    case HardwareMessage_Type_WRITE_GPIOS:
        // Print notification to LCD screen
        screen->print("Write GPIOs");

        for (uint8_t i = 0; i < NUM_GPIOS; i++) {
            uint64_t mask = 1 << i;
            if (p.gpio_mask & mask) {
                digitalWrite(i, (p.gpio_value & mask) ? 1 : 0);
                pinMode(i, OUTPUT);
            }
        }
        break;
    case HardwareMessage_Type_READ_GPIOS: {
        // Print notification to LCD screen
        screen->print("Read GPIOs");

        uint64_t res = 0;
        for (uint8_t i = 0; i < NUM_GPIOS; i++) {
            uint64_t mask = 1 << i;
            if (p.gpio_mask & mask) {
                pinMode(i, INPUT_PULLUP);
                if (digitalRead(i))
                    res |= (1 << i);
            }
        }

        // Send the reply
        HardwareMessage reply;
        CLEAR_STRUCT(reply);
        reply.typ = HardwareMessage_Type_READ_GPIOS_REPLY;
        reply.gpio_value = res;
        MeshPacket *p = allocDataProtobuf(reply);
        setReplyTo(p, req);
        service.sendToMesh(p);
        break;
    }
    default:
        DEBUG_MSG("Hardware operation %d not yet implemented! FIXME\n", p.typ);
        break;
    }
    return true; // handled
}
