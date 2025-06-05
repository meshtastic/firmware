#include "./DEPG0290BNS800.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

// Describes the operation performed when a "fast refresh" is performed
// Source: custom, with DEPG0150BNS810 as a reference
static const uint8_t LUT_FAST[] = {
    // 1     2     3     4
    0x40, 0x00, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // B2B (Existing black pixels)
    0x00, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // B2W (New white pixels)
    0x00, 0x40, 0x40, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // W2B (New black pixels)
    0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // W2W (Existing white pixels)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // VCOM

    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 1. Tap existing black pixels back into place
    0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 2. Move new pixels
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // 3. New pixels, and also existing black pixels
    0x02, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, // 4. All pixels, then cooldown
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //

    0x22, 0x22, 0x22, 0x22, 0x22, 0x22, 0x00, 0x00, 0x00,
};

// How strongly the pixels are pulled and pushed
void DEPG0290BNS800::configVoltages()
{
    switch (updateType) {
    case FAST:
        // Listed as "typical" in datasheet
        sendCommand(0x04);
        sendData(0x41); // VSH1 15V
        sendData(0x00); // VSH2 NA
        sendData(0x32); // VSL -15V
        break;

    case FULL:
    default:
        // From OTP memory
        break;
    }
}

// Load settings about how the pixels are moved from old state to new state during a refresh
// - manually specified,
// - or with stored values from displays OTP memory
void DEPG0290BNS800::configWaveform()
{
    switch (updateType) {
    case FAST:
        sendCommand(0x3C); // Border waveform:
        sendData(0x60);    // Actively hold screen border during update

        sendCommand(0x32);                    // Write LUT register from MCU:
        sendData(LUT_FAST, sizeof(LUT_FAST)); // (describes operation for a FAST refresh)
        break;

    case FULL:
    default:
        // From OTP memory
        break;
    }
}

// Describes the sequence of events performed by the displays controller IC during a refresh
// Includes "power up", "load settings from memory", "update the pixels", etc
void DEPG0290BNS800::configUpdateSequence()
{
    switch (updateType) {
    case FAST:
        sendCommand(0x22); // Set "update sequence"
        sendData(0xCF);    // Differential, use manually loaded waveform
        break;

    case FULL:
    default:
        sendCommand(0x22); // Set "update sequence"
        sendData(0xF7);    // Non-differential, load waveform from OTP
        break;
    }
}

// Once the refresh operation has been started,
// begin periodically polling the display to check for completion, using the normal Meshtastic threading code
// Only used when refresh is "async"
void DEPG0290BNS800::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(50, 450); // At least 450ms for fast refresh
    case FULL:
    default:
        return beginPolling(100, 3000); // At least 3 seconds for full refresh
    }
}

// For this display, we do not need to re-write the new image.
// We're overriding SSD16XX::finalizeUpdate to make this small optimization.
// The display does also work just fine with the generic SSD16XX method, though.
void DEPG0290BNS800::finalizeUpdate()
{
    // Put a copy of the image into the "old memory".
    // Used with differential refreshes (e.g. FAST update), to determine which px need to move, and which can remain in place
    // We need to keep the "old memory" up to date, because don't know whether next refresh will be FULL or FAST etc.
    if (updateType != FULL) {
        // writeNewImage(); // Not required for this display
        writeOldImage();
        sendCommand(0x7F); // Terminate image write without update
        wait();
    }

    // Enter deep-sleep to save a few µA
    // Waking from this requires that display's reset pin is broken out
    if (pin_rst != 0xFF)
        deepSleep();
}
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS