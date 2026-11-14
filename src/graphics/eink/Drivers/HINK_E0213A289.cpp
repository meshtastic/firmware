#include "./HINK_E0213A289.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

// Map the display controller IC's output to the connected panel
void HINK_E0213A289::configScanning()
{
    // "Driver output control"
    // Scan gates from 0 to 249 (vertical resolution 250px)
    sendCommand(0x01);
    sendData(0xF9); // Maximum gate # (249, bits 0-7)
    sendData(0x00); // Maximum gate # (bit 8)
    sendData(0x00); // (Do not invert scanning order)
}

// Specify which information is used to control the sequence of voltages applied to move the pixels
// - For this display, configUpdateSequence() specifies that a suitable LUT will be loaded from
//   the controller IC's OTP memory, when the update procedure begins.
void HINK_E0213A289::configWaveform()
{
    sendCommand(0x3C); // Border waveform:
    sendData(0x05);    // Screen border should follow LUT1 waveform (actively drive pixels white)

    sendCommand(0x18); // Temperature sensor:
    sendData(0x80);    // Use internal temperature sensor to select an appropriate refresh waveform
}

// Describes the sequence of events performed by the displays controller IC during a refresh
// Includes "power up", "load settings from memory", "update the pixels", etc
void HINK_E0213A289::configUpdateSequence()
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
void HINK_E0213A289::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(50, 500); // At least 500ms for fast refresh
    case FULL:
    default:
        return beginPolling(100, 1000); // At least 1 second for full refresh (quick; display only blinks pixels once)
    }
}
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS