#include "./E0213A367.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

// Map the display controller IC's output to the connected panel
void E0213A367::configScanning()
{
    // "Driver output control"
    // Scan gates from 0 to 249 (vertical resolution 250px)
    sendCommand(0x01);
    sendData(0xF9);
    sendData(0x00);
}

// Specify which information is used to control the sequence of voltages applied to move the pixels
void E0213A367::configWaveform()
{
    // This command (0x37) is poorly documented
    // As of July 2025, the datasheet for this display's controller IC is unavailable
    // The values are supplied by Heltec, who presumably have privileged access to information from the display manufacturer
    // Datasheet for the similar SSD1680 IC hints at the function of this command:

    // "Spare VCOM OTP selection":
    // Unclear why 0x40 is set. Sane values for related SSD1680 seem to be 0x80 or 0x00.
    // Maybe value is redundant? No noticeable impact when set to 0x00.
    // We'll leave it set to 0x40, following Heltec's lead, just in case.

    // "Display Mode"
    // Seems to specify whether a waveform stored in OTP should use display mode 1 or 2 (full refresh or differential refresh)

    // Unusual that waveforms are programmed to OTP, but this meta information is not ..?

    sendCommand(0x37); // "Write Register for Display Option" ?
    sendData(0x40);    // "Spare VCOM OTP selection" ?
    sendData(0x80);    // "Display Mode for WS[7:0]" ?
    sendData(0x03);    // "Display Mode for WS[15:8]" ?
    sendData(0x0E);    // "Display Mode [23:16]" ?

    switch (updateType) {
    case FAST:
        sendCommand(0x3C); // Border waveform:
        sendData(0x81);    // As specified by Heltec. Actually VCOM (0x80)?. Bit 0 seems redundant here.
        break;
    case FULL:
    default:
        sendCommand(0x3C); // Border waveform:
        sendData(0x01);    // Follow LUT 1 (blink same as white pixels)
        break;
    }
}

// Tell controller IC which operations to run
void E0213A367::configUpdateSequence()
{
    switch (updateType) {
    case FAST:
        sendCommand(0x22); // Set "update sequence"
        sendData(0xFF);    // Will load LUT from OTP memory, Display mode 2 "differential refresh"
        break;
    case FULL:
    default:
        sendCommand(0x22); // Set "update sequence"
        sendData(0xF7);    // Will load LUT from OTP memory, Display mode 1 "full refresh"
        break;
    }
}

// Once the refresh operation has been started,
// begin periodically polling the display to check for completion, using the normal Meshtastic threading code
// Only used when refresh is "async"
void E0213A367::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(50, 500); // At least 500ms for fast refresh
    case FULL:
    default:
        return beginPolling(100, 1500); // At least 1.5 seconds for full refresh
    }
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS