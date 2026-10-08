#include "./HeltecVME290.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

// OTP LUT (ZJY128296 approach). FAST 0xFF is a no-op on 15-22243-M0282 panels.
void HeltecVME290::configWaveform()
{
    sendCommand(0x3C); // Border waveform:
    sendData(0x05);    // Screen border follows LUT1 (drive white)

    sendCommand(0x18); // Temperature sensor:
    sendData(0x80);    // Use internal temp sensor for waveform selection
}

void HeltecVME290::configUpdateSequence()
{
    switch (updateType) {
    case FAST:
        sendCommand(0x22); // Set "update sequence"
        sendData(0xFF);    // Load LUT from OTP, differential refresh (mode 2)
        break;

    case FULL:
    default:
        sendCommand(0x22); // Set "update sequence"
        sendData(0xF7);    // Load LUT from OTP, full refresh
        break;
    }
}

void HeltecVME290::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(50, 300); // ~300ms for fast refresh
    case FULL:
    default:
        return beginPolling(100, 2000); // ~2s for full refresh
    }
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
