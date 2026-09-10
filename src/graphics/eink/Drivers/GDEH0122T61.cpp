#include "./GDEH0122T61.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

void GDEH0122T61::configScanning()
{
    sendCommand(0x01);
    sendData(0xAF); // Scan until gate 175 (176px vertical resolution, low byte)
    sendData(0x00); // high byte
    sendData(0x00);
}

void GDEH0122T61::configWaveform()
{
    sendCommand(0x3C);
    sendData(0x05);

    sendCommand(0x18);
    sendData(0x80);
}

void GDEH0122T61::configUpdateSequence()
{
    switch (updateType) {
    case FAST:
        sendCommand(0x22);
        sendData(0xFF);
        break;
    case FULL:
    default:
        sendCommand(0x22);
        sendData(0xF7);
        break;
    }
}

void GDEH0122T61::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(50, 250);
    case FULL:
    default:
        return beginPolling(100, 1500);
    }
}
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
