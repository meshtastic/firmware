#include "./LCMEN2R13ECC1.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

// Map the display controller IC's output to the connected panel
void LCMEN2R13ECC1::configScanning()
{
    // "Driver output control"
    sendCommand(0x01);
    sendData(0xF9);
    sendData(0x00);
    sendData(0x00);

    // To-do: delete this method?
    // Values set here might be redundant: F9, 00, 00 seems to be default
}

// Specify which information is used to control the sequence of voltages applied to move the pixels
// - For this display, configUpdateSequence() specifies that a suitable LUT will be loaded from
//   the controller IC's OTP memory, when the update procedure begins.
void LCMEN2R13ECC1::configWaveform()
{
    switch (updateType) {
    case FAST:
        sendCommand(0x3C); // Border waveform:
        sendData(0x85);
        break;

    case FULL:
    default:
        // From OTP memory
        break;
    }
}

void LCMEN2R13ECC1::configUpdateSequence()
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
void LCMEN2R13ECC1::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(50, 800); // At least 500ms for fast refresh
    case FULL:
    default:
        return beginPolling(100, 2500); // At least 2 seconds for full refresh
    }
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS