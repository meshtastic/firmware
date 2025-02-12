#include "RemoteHardwareModule.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"
#include <Throttle.h>

#define NUM_GPIOS 64

// Because (FIXME) we currently don't tell API clients status on sent messages
// we need to throttle our sending, so that if a gpio is bouncing up and down we
// don't generate more messages than the net can send. So we limit watch messages to
// a max of one change per 30 seconds
#define WATCH_INTERVAL_MSEC (30 * 1000)

// Tests for access to read from or write to a specified GPIO pin
static bool pinAccessAllowed(uint64_t mask, uint8_t pin)
{
    // If undefined pin access is allowed, don't check the pin and just return true
    if (moduleConfig.remote_hardware.allow_undefined_pin_access) {
        return true;
    }

    // Test to see if the pin is in the list of allowed pins and return true if found
    if (mask & (1ULL << pin)) {
        return true;
    }

    return false;
}

/// Set pin modes for every set bit in a mask
static void pinModes(uint64_t mask, uint8_t mode, uint64_t maskAvailable)
{
    for (uint64_t i = 0; i < NUM_GPIOS; i++) {
        if (mask & (1ULL << i)) {
            if (pinAccessAllowed(maskAvailable, i)) {
                pinMode(i, mode);
            }
        }
    }
}

/// Read all the pins mentioned in a mask
static uint64_t digitalReads(uint64_t mask, uint64_t maskAvailable)
{
    uint64_t res = 0;

    pinModes(mask, INPUT_PULLUP, maskAvailable);

    for (uint64_t i = 0; i < NUM_GPIOS; i++) {
        uint64_t m = 1ULL << i;
        if (mask & m && pinAccessAllowed(maskAvailable, i)) {
            if (digitalRead(i)) {
                res |= m;
            }
        }
    }

    return res;
}

RemoteHardwareModule::RemoteHardwareModule()
    : ProtobufModule("remotehardware", meshtastic_PortNum_REMOTE_HARDWARE_APP, &meshtastic_HardwareMessage_msg),
      concurrency::OSThread("RemoteHardware")
{
    // restrict to the gpio channel for rx
    boundChannel = Channels::gpioChannel;

    // Pull available pin allowlist from config and build a bitmask out of it for fast comparisons later
    for (uint8_t i = 0; i < 4; i++) {
        availablePins += 1ULL << moduleConfig.remote_hardware.available_pins[i].gpio_pin;
    }
}

bool RemoteHardwareModule::handleReceivedProtobuf(const meshtastic_MeshPacket &req, meshtastic_HardwareMessage *pptr)
{
    if (moduleConfig.remote_hardware.enabled) {
        auto p = *pptr;
        LOG_INFO("Received RemoteHardware type=%d", p.type);

        switch (p.type) {
        case meshtastic_HardwareMessage_Type_WRITE_GPIOS: {
            // Print notification to LCD screen
            screen->print("Write GPIOs\n");

            pinModes(p.gpio_mask, OUTPUT, availablePins);
            for (uint8_t i = 0; i < NUM_GPIOS; i++) {
                uint64_t mask = 1ULL << i;
                if (p.gpio_mask & mask && pinAccessAllowed(availablePins, i)) {
                    digitalWrite(i, (p.gpio_value & mask) ? 1 : 0);
                }
            }

            break;
        }

        case meshtastic_HardwareMessage_Type_READ_GPIOS: {
            // Print notification to LCD screen
            if (screen)
                screen->print("Read GPIOs\n");

            uint64_t res = digitalReads(p.gpio_mask, availablePins);

            // Send the reply
            meshtastic_HardwareMessage r = meshtastic_HardwareMessage_init_default;
            r.type = meshtastic_HardwareMessage_Type_READ_GPIOS_REPLY;
            r.gpio_value = res;
            r.gpio_mask = p.gpio_mask;
            meshtastic_MeshPacket *p2 = allocDataProtobuf(r);
            setReplyTo(p2, req);
            myReply = p2;
            break;
        }

        case meshtastic_HardwareMessage_Type_WATCH_GPIOS: {
            watchGpios = p.gpio_mask;
            lastWatchMsec = 0; // Force a new publish soon
            previousWatch =
                ~watchGpios;   // generate a 'previous' value which is guaranteed to not match (to force an initial publish)
            enabled = true;    // Let our thread run at least once
            setInterval(2000); // Set a new interval so we'll run soon
            LOG_INFO("Now watching GPIOs 0x%llx", watchGpios);
            break;
        }

        case meshtastic_HardwareMessage_Type_READ_GPIOS_REPLY:
        case meshtastic_HardwareMessage_Type_GPIOS_CHANGED:
            break; // Ignore - we might see our own replies

        default:
            LOG_ERROR("Hardware operation %d not yet implemented! FIXME", p.type);
            break;
        }
    }

    return false;
}

int32_t RemoteHardwareModule::runOnce()
{
    if (moduleConfig.remote_hardware.enabled && watchGpios) {

        if (!Throttle::isWithinTimespanMs(lastWatchMsec, WATCH_INTERVAL_MSEC)) {
            uint64_t curVal = digitalReads(watchGpios, availablePins);
            lastWatchMsec = millis();

            if (curVal != previousWatch) {
                previousWatch = curVal;
                LOG_INFO("Broadcast GPIOS 0x%llx changed!", curVal);

                // Something changed!  Tell the world with a broadcast message
                meshtastic_HardwareMessage r = meshtastic_HardwareMessage_init_default;
                r.type = meshtastic_HardwareMessage_Type_GPIOS_CHANGED;
                r.gpio_value = curVal;
                meshtastic_MeshPacket *p = allocDataProtobuf(r);
                service->sendToMesh(p);
            }
        }
    } else {
        // No longer watching anything - stop using CPU
        return disable();
    }

    return 2000; // Poll our GPIOs every 2000ms
}