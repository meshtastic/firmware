#include "./E0213A367.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

// Map the display controller IC's output to the connected panel
void E0213A367::configScanning()
{
    // "Driver output control"
    sendCommand(0x01);
    sendData(0xF9);
    sendData(0x00);
    // Values set here might be redundant: F9, 00 seems to be default
}

// Specify which information is used to control the sequence of voltages applied to move the pixels
// - For this display, configUpdateSequence() specifies that a suitable LUT will be loaded from
//   the controller IC's OTP memory, when the update procedure begins.
void E0213A367::configWaveform()
{
    sendCommand(0x37);  // Waveform ID register
    sendData(0x40); // ByteA
    sendData(0x80); // ByteB        DM[7:0]
    sendData(0x03); // ByteC        DM[[15:8]
    sendData(0x0E); // ByteD        DM[[23:16]

    switch (updateType) {
    case FAST:
        sendCommand(0x3C); // Border waveform:
        sendData(0x81);
        break;
    case FULL:
        sendCommand(0x3C); // Border waveform:
        sendData(0x01);
    default:
        // From OTP memory
        break;
    }
}

void E0213A367::configUpdateSequence()
{
    switch (updateType) {
    case FAST:
        sendCommand(0x22); 
        sendData(0xFF);
        break;
    case FULL:
    default:
        sendCommand(0x22); // Set "update sequence"
        sendData(0xF7);
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
        return beginPolling(100, 2000); // At least 2 seconds for full refresh
    }
}
void E0213A367::configFullscreen()
{
     // Placing this code in a separate method because it's probably pretty consistent between displays
    // Should make it tidier to override SSD16XX::configure

    // Define the boundaries of the "fullscreen" region, for the controller IC
    static const uint16_t sx = bufferOffsetX; // Notice the offset
    static const uint16_t sy = 0;
    static const uint16_t ex = bufferRowSize + bufferOffsetX - 1; // End is "max index", not "count". Minus 1 handles this
    static const uint16_t ey = height;

    // Split into bytes
    static const uint8_t sy1 = sy & 0xFF;
    static const uint8_t ey1 = ey & 0xFF;

    // Data entry mode - Left to Right, Top to Bottom
    sendCommand(0x11);
    sendData(0x03);

    // Select controller IC memory region to display a fullscreen image
    sendCommand(0x44); // Memory X start - end
    sendData(sx);
    sendData(ex);
    sendCommand(0x45); // Memory Y start - end
    sendData(sy1);
    sendData(ey1);

    // Place the cursor at the start of this memory region, ready to send image data x=0 y=0
    sendCommand(0x4E); // Memory cursor X
    sendData(sx);
    sendCommand(0x4F); // Memory cursor y
    sendData(sy1);
}
void E0213A367::finalizeUpdate()
{
    // Put a copy of the image into the "old memory".
    // Used with differential refreshes (e.g. FAST update), to determine which px need to move, and which can remain in place
    // We need to keep the "old memory" up to date, because don't know whether next refresh will be FULL or FAST etc.
    if (updateType != FULL) {
        writeNewImage(); // Only required by some controller variants. Todo: Override just for GDEY0154D678?
        writeOldImage();
        sendCommand(0x7F); // Terminate image write without update
        wait();
    }

    //After waking up from sleep mode, the local refresh is abnormal, which may be due to the loss of data in RAM.
    // if ((pin_rst != 0xFF) && (updateType ==FULL))
    //     deepSleep();
}

void E0213A367::deepSleep()
{
    sendCommand(0x10); // Enter deep sleep
    sendData(0x03);    // Will not retain image RAM
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS