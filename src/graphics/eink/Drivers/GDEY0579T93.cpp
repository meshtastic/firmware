#include "./GDEY0579T93.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

void GDEY0579T93::configScanning()
{
    sendCommand(0x01);
    sendData(0x0F); // 271, low byte
    sendData(0x01); // 271, high byte
    sendData(0x00);
}

void GDEY0579T93::configWaveform()
{
    sendCommand(0x3C);
    sendData(0x01);

    sendCommand(0x18);
    sendData(0x80);
}

void GDEY0579T93::configUpdateSequence()
{
    switch (updateType) {
    case FAST:
        sendCommand(0x21);
        sendData(0x00);
        sendData(0x00);

        sendCommand(0x22);
        sendData(0xFF);
        break;
    case FULL:
    default:
        sendCommand(0x21);
        sendData(0x40);
        sendData(0x00);

        sendCommand(0x22);
        sendData(0xF7);
        break;
    }
}

void GDEY0579T93::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(100, 2000);
    case FULL:
    default:
        return beginPolling(150, 5000);
    }
}
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
