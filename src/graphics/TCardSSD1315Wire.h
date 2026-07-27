#pragma once

#include <SSD1306Wire.h>

class TCardSSD1315Wire : public SSD1306Wire
{
  public:
    TCardSSD1315Wire(uint8_t address, HW_I2C i2cBus, uint8_t pageOffset)
        : SSD1306Wire(address, -1, -1, GEOMETRY_72_40, i2cBus, 100000), address(address), pageOffset(pageOffset), wire(&Wire)
    {
    }

    void display() override
    {
        constexpr uint8_t transferSize = 16;
        const uint8_t columnOffset = (128 - width()) / 2;

        writeCommand(COLUMNADDR);
        writeCommand(columnOffset);
        writeCommand(columnOffset + width() - 1);
        writeCommand(PAGEADDR);
        writeCommand(pageOffset);
        writeCommand(pageOffset + height() / 8 - 1);

        uint16_t position = 0;
        while (position < displayBufferSize) {
            const uint8_t chunkSize = std::min<uint16_t>(transferSize, displayBufferSize - position);
            wire->beginTransmission(address);
            wire->write(0x40);
            for (uint8_t i = 0; i < chunkSize; ++i) {
                wire->write(buffer[position++]);
            }
            wire->endTransmission();
        }
    }

  protected:
    void sendInitCommands() override
    {
        writeCommand(DISPLAYOFF);
        writeCommand(SETDISPLAYCLOCKDIV);
        writeCommand(0x80);
        writeCommand(SETMULTIPLEX);
        writeCommand(0x3f);
        writeCommand(SETDISPLAYOFFSET);
        writeCommand(0x00);
        writeCommand(SETSTARTLINE);
        writeCommand(CHARGEPUMP);
        writeCommand(0x14);
        writeCommand(MEMORYMODE);
        writeCommand(0x00);
        writeCommand(SEGREMAP);
        writeCommand(COMSCANINC);
        writeCommand(SETCOMPINS);
        writeCommand(0x12);
        writeCommand(SETCONTRAST);
        writeCommand(0xcf);
        writeCommand(SETPRECHARGE);
        writeCommand(0xf1);
        writeCommand(SETVCOMDETECT);
        writeCommand(0x40);
        writeCommand(DISPLAYALLON_RESUME);
        writeCommand(NORMALDISPLAY);
        writeCommand(0x2e);
        if (!delayPoweron) {
            writeCommand(DISPLAYON);
        }

        writeCommand(0xad);
        writeCommand(0x10);
    }

  private:
    void writeCommand(uint8_t command)
    {
        wire->beginTransmission(address);
        wire->write(0x80);
        wire->write(command);
        wire->endTransmission();
    }

    uint8_t address;
    uint8_t pageOffset;
    TwoWire *wire;
};
