#include "./ZJY128296_029EAAMFGN.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

// Map the display controller IC's output to the connected panel
void ZJY128296_029EAAMFGN::configScanning()
{
    // "Driver output control"
    // Scan gates from 0 to 295 (vertical resolution 296px)
    sendCommand(0x01);
    sendData(0x27); // Number of gates (295, bits 0-7)
    sendData(0x01); // Number of gates (295, bit 8)
    sendData(0x00); // (Do not invert scanning order)
}

// Specify which information is used to control the sequence of voltages applied to move the pixels
// - For this display, configUpdateSequence() specifies that a suitable LUT will be loaded from
//   the controller IC's OTP memory, when the update procedure begins.
void ZJY128296_029EAAMFGN::configWaveform()
{
    sendCommand(0x3C); // Border waveform:
    sendData(0x05);    // Screen border should follow LUT1 waveform (actively drive pixels white)

    sendCommand(0x18); // Temperature sensor:
    sendData(0x80);    // Use internal temperature sensor to select an appropriate refresh waveform
}

void ZJY128296_029EAAMFGN::configUpdateSequence()
{
    switch (updateType) {
    case FAST:
        sendCommand(0x22); // Set "update sequence"
        sendData(0xFF);    // Will load LUT from OTP memory, Display mode 2 "differential refresh"
        break;

    case FULL:
    default:
        sendCommand(0x22); // Set "update sequence"
        sendData(0xF7);    // Will load LUT from OTP memory
        break;
    }
}

// Once the refresh operation has been started,
// begin periodically polling the display to check for completion, using the normal Meshtastic threading code
// Only used when refresh is "async"
void ZJY128296_029EAAMFGN::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(50, 300); // At least 300ms for fast refresh
    case FULL:
    default:
        return beginPolling(100, 2000); // At least 2 seconds for full refresh
    }
}
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS