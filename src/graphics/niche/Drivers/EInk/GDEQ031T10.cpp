#include "./GDEQ031T10.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Drivers;

void GDEQ031T10::begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst)
{
    UC8175::begin(spi, pin_dc, pin_cs, pin_busy, pin_rst);
    // Recover a V1.1 panel left asleep by a previous firmware.
    if (pin_rst != static_cast<uint8_t>(-1))
        UC8175::reset();
}

void GDEQ031T10::reset()
{
    // Software reset also works on V1.0, whose panel reset is not connected.
    sendCommand(0x00);
    sendData(0x1E);
    sendData(0x0D);
    delay(1);
}

void GDEQ031T10::configCommon()
{
    if (!hasPreviousBuffer)
        updateType = FULL;
    sendCommand(0x00);
    sendData(0x1F);
    sendData(0x0D);
    sendCommand(0xE0);
    sendData(0x02);
}

void GDEQ031T10::configFull()
{
    sendCommand(0xE5);
    sendData(0x5A);
    sendCommand(0x50);
    sendData(0x97);
}

void GDEQ031T10::configFast()
{
    sendCommand(0xE5);
    sendData(0x79);
    sendCommand(0x50);
    sendData(0xD7);
}

void GDEQ031T10::writeNewImage()
{
    UC8175::writeNewImage();
    if (updateType == FAST) {
        sendCommand(0x91);
        setWindow();
    }
    powerOn();
}

void GDEQ031T10::setWindow()
{
    const uint8_t window[] = {0, 239, 0, 0, 1, 63, 1};
    sendCommand(0x90);
    sendData(window, sizeof(window));
}

void GDEQ031T10::writeImage(uint8_t command, const uint8_t *image)
{
    sendCommand(0x91);
    setWindow();
    UC8175::writeImage(command, image);
    sendCommand(0x92);
}

void GDEQ031T10::finalizeUpdate()
{
    if (updateType == FAST)
        sendCommand(0x92);
    // Preserve panel RAM; hardware hibernation is unreliable on V1.1.
    powerOff();
}

#endif
