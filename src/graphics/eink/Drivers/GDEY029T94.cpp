#include "./GDEY029T94.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

void GDEY029T94::configScanning()
{
    sendCommand(0x01);
    sendData(0x27); // 295, low byte
    sendData(0x01); // 295, high byte
    sendData(0x00);
}

void GDEY029T94::configWaveform()
{
    sendCommand(0x3C);
    sendData(0x05);

    sendCommand(0x18);
    sendData(0x80);
}

void GDEY029T94::configUpdateSequence()
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

void GDEY029T94::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(50, 300);
    case FULL:
    default:
        return beginPolling(100, 2000);
    }
}
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
