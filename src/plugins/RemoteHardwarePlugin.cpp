#include "RemoteHardwarePlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

RemoteHardwarePlugin remoteHardwarePlugin;

#define NUM_GPIOS 64


bool RemoteHardwarePlugin::handleReceivedProtobuf(const MeshPacket &req, const HardwareMessage &p)
{
    switch (p.typ) {
    case HardwareMessage_Type_WRITE_GPIOS:
        // Print notification to LCD screen
        screen->print("Write GPIOs\n");

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
        screen->print("Read GPIOs\n");

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
        HardwareMessage reply = HardwareMessage_init_default;
        reply.typ = HardwareMessage_Type_READ_GPIOS_REPLY;
        reply.gpio_value = res;
        MeshPacket *p = allocDataProtobuf(reply);
        setReplyTo(p, req);
        service.sendToMesh(p);
        break;
    }

    case HardwareMessage_Type_READ_GPIOS_REPLY:
    case HardwareMessage_Type_GPIOS_CHANGED:
        break; // Ignore - we might see our own replies

    default:
        DEBUG_MSG("Hardware operation %d not yet implemented! FIXME\n", p.typ);
        break;
    }
    return true; // handled
}
