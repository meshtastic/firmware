#include "./HINK_E042A87.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

// Load settings about how the pixels are moved from old state to new state during a refresh
// - manually specified,
// - or with stored values from displays OTP memory
void HINK_E042A87::configWaveform()
{
    sendCommand(0x3C); // Border waveform:
    sendData(0x01);    // Follow LUT for VSH1

    sendCommand(0x18); // Temperature sensor:
    sendData(0x80);    // Use internal temperature sensor to select an appropriate refresh waveform
}

// Describes the sequence of events performed by the displays controller IC during a refresh
// Includes "power up", "load settings from memory", "update the pixels", etc
void HINK_E042A87::configUpdateSequence()
{
    switch (updateType) {
    case FAST:
        sendCommand(0x21); // Use both "old" and "new" image memory (differential)
        sendData(0x00);
        sendData(0x00);

        sendCommand(0x22); // Set "update sequence"
        sendData(0xFF);    // Differential, load waveform from OTP
        break;

    case FULL:
    default:
        sendCommand(0x21); // Bypass "old" image memory (non-differential)
        sendData(0x40);
        sendData(0x00);

        sendCommand(0x22); // Set "update sequence":
        sendData(0xF7);    // Non-differential, load waveform from OTP
        break;
    }
}

// Once the refresh operation has been started,
// begin periodically polling the display to check for completion, using the normal Meshtastic threading code
// Only used when refresh is "async"
void HINK_E042A87::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(50, 1000); // At least 1 second, then check every 50ms
    case FULL:
    default:
        return beginPolling(100, 3500); // At least 3.5 seconds, then check every 100ms
    }
}
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS