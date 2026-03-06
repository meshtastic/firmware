#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "./GDEW0102T4.h"

#include <cstring>

using namespace NicheGraphics::Drivers;

// LUTs from GxEPD2_102.cpp (GDEW0102T4 / UC8175).
static const uint8_t LUT_W_FULL[] = {
    0x60, 0x5A, 0x5A, 0x00, 0x00, 0x01, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
};

static const uint8_t LUT_B_FULL[] = {
    0x90, 0x5A, 0x5A, 0x00, 0x00, 0x01, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
};

static const uint8_t LUT_W_FAST[] = {
    0x60, 0x01, 0x01, 0x00, 0x00, 0x01, //
    0x80, 0x12, 0x00, 0x00, 0x00, 0x01, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
};

static const uint8_t LUT_B_FAST[] = {
    0x90, 0x01, 0x01, 0x00, 0x00, 0x01, //
    0x40, 0x14, 0x00, 0x00, 0x00, 0x01, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, //
};

GDEW0102T4::GDEW0102T4() : UC8175(width, height, supported) {}

void GDEW0102T4::setFastConfig(FastConfig cfg)
{
    // Clamp out only clearly invalid PLL settings.
    if (cfg.reg30 < 0x05)
        cfg.reg30 = 0x05;
    fastConfig = cfg;
}

GDEW0102T4::FastConfig GDEW0102T4::getFastConfig() const
{
    return fastConfig;
}

void GDEW0102T4::configCommon()
{
    // Init path aligned with GxEPD2_GDEW0102T4 (UC8175 family).
    sendCommand(0xD2);
    sendData(0x3F);

    sendCommand(0x00);
    sendData(0x6F);

    sendCommand(0x01);
    sendData(0x03);
    sendData(0x00);
    sendData(0x2B);
    sendData(0x2B);

    sendCommand(0x06);
    sendData(0x3F);

    sendCommand(0x2A);
    sendData(0x00);
    sendData(0x00);

    sendCommand(0x30); // PLL / drive clock
    sendData(0x13);

    sendCommand(0x50);//Last border/data interval; subtle but can affect artifacts
    sendData(0x57);

    sendCommand(0x60);
    sendData(0x22);

    sendCommand(0x61);
    sendData(width);
    sendData(height);

    sendCommand(0x82); // VCOM DC setting
    sendData(0x12);

    sendCommand(0xE3);
    sendData(0x33);
}

void GDEW0102T4::configFull()
{
    sendCommand(0x23);
    sendData(LUT_W_FULL, sizeof(LUT_W_FULL));
    sendCommand(0x24);
    sendData(LUT_B_FULL, sizeof(LUT_B_FULL));

    powerOn();
}

void GDEW0102T4::configFast()
{
    uint8_t lutW[sizeof(LUT_W_FAST)];
    uint8_t lutB[sizeof(LUT_B_FAST)];
    memcpy(lutW, LUT_W_FAST, sizeof(LUT_W_FAST));
    memcpy(lutB, LUT_B_FAST, sizeof(LUT_B_FAST));

    // Second stage duration bytes are the main "darkness vs ghosting" control for this panel.
    lutW[7] = fastConfig.lutW2;
    lutB[7] = fastConfig.lutB2;

    sendCommand(0x30);
    sendData(fastConfig.reg30);

    sendCommand(0x50);
    sendData(fastConfig.reg50);

    sendCommand(0x82);
    sendData(fastConfig.reg82);

    sendCommand(0x23);
    sendData(lutW, sizeof(lutW));
    sendCommand(0x24);
    sendData(lutB, sizeof(lutB));

    powerOn();
}

void GDEW0102T4::writeOldImage()
{
    // On this panel, FULL refresh is most reliable when "old image" is all white.
    if (updateType == FULL) {
        sendCommand(0x10);
        for (uint32_t i = 0; i < bufferSize; ++i)
            sendData((uint8_t)0xFF);
        return;
    }

    // FAST refresh uses differential data (previous frame as old image).
    if (previousBuffer) {
        writeImage(0x10, previousBuffer);
    } else {
        writeImage(0x10, buffer);
    }
}

void GDEW0102T4::finalizeUpdate()
{
    // Keep panel out of deep-sleep between updates for better reliability of repeated FAST refresh.
    powerOff();
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
