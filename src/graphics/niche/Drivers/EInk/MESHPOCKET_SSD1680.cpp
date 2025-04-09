#include "./MESHPOCKET_SSD1680.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

// Map the display controller IC's output to the connected panel
void MESHPOCKET_SSD1680::configScanning()
{
    // "Driver output control"
    sendCommand(0x01);
    sendData(0xF9);
    sendData(0x00);
    sendData(0x00);

    // To-do: delete this method?
    // Values set here might be redundant: C7, 00, 00 seems to be default
}

// Specify which information is used to control the sequence of voltages applied to move the pixels
// - For this display, configUpdateSequence() specifies that a suitable LUT will be loaded from
//   the controller IC's OTP memory, when the update procedure begins.
void MESHPOCKET_SSD1680::configWaveform()
{
    sendCommand(0x3C); // Border waveform:
    sendData(0x85);    // Screen border should follow LUT1 waveform (actively drive pixels white)

    sendCommand(0x18); // Temperature sensor:
    sendData(0x80);    // Use internal temperature sensor to select an appropriate refresh waveform
}

void MESHPOCKET_SSD1680::configUpdateSequence()
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
void MESHPOCKET_SSD1680::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(50, 500); // At least 500ms for fast refresh
    case FULL:
    default:
        return beginPolling(100, 2000); // At least 2 seconds for full refresh
    }
}

void MESHPOCKET_SSD1680::update(uint8_t *imageData, UpdateTypes type)
{
    this->updateType = type;
    this->buffer = imageData;

    reset();

    configFullscreen();
    configScanning(); // Virtual, unused by base class
    configVoltages(); // Virtual, unused by base class
    configWaveform(); // Virtual, unused by base class
    wait();

    if (updateType == FULL) {
        sendCommand(0x12);  // Begin executing the update
        wait();
        configFullscreen();
        wait();
        writeNewImage();
        writeOldImage();
    } else {
        writeNewImage();
    }

    configUpdateSequence();
    sendCommand(0x20); // Begin executing the update

    // Let the update run async, on display hardware. Base class will poll completion, then finalize.
    // For a blocking update, call await after update
    detachFromUpdate();
}

// void MESHPOCKET_SSD1680::finalizeUpdate()
// {
//     // Put a copy of the image into the "old memory".
//     // Used with differential refreshes (e.g. FAST update), to determine which px need to move, and which can remain in place
//     // We need to keep the "old memory" up to date, because don't know whether next refresh will be FULL or FAST etc.
//     if (updateType != FULL) {
//         // writeNewImage(); // Not required for this display
//         writeOldImage();
//         sendCommand(0x7F); // Terminate image write without update
//         wait();
//     }
// }
#endif   // MESHTASTIC_INCLUDE_NICHE_GRAPHICS