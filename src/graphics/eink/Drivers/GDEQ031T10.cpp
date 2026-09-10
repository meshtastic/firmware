#include "./GDEQ031T10.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

void GDEQ031T10::configScanning()
{
    sendCommand(0x01);
    sendData(0x3F); // 319, low byte
    sendData(0x01); // 319, high byte
    sendData(0x00);
}

void GDEQ031T10::configWaveform()
{
    sendCommand(0x3C);
    sendData(0x01);

    sendCommand(0x18);
    sendData(0x80);
}

void GDEQ031T10::configUpdateSequence()
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

void GDEQ031T10::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(50, 400);
    case FULL:
    default:
        return beginPolling(100, 2500);
    }
}
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
