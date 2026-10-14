#include "./GDEY042T81.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

void GDEY042T81::configWaveform()
{
    sendCommand(0x3C);
    sendData(0x01);

    sendCommand(0x18);
    sendData(0x80);
}

void GDEY042T81::configUpdateSequence()
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

void GDEY042T81::detachFromUpdate()
{
    switch (updateType) {
    case FAST:
        return beginPolling(50, 1000);
    case FULL:
    default:
        return beginPolling(100, 3500);
    }
}
#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
