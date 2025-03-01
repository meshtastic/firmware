/*

E-Ink base class for displays based on SSD16XX

Most (but not all) SPI E-Ink displays use this family of controller IC.
Implementing new SSD16XX displays should be fairly painless.
See DEPG0154BNS800 and DEPG0290BNS800 for examples.

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./EInk.h"

namespace NicheGraphics::Drivers
{

class SSD16XX : public EInk
{
  public:
    SSD16XX(uint16_t width, uint16_t height, UpdateTypes supported, uint8_t bufferOffsetX = 0);
    virtual void begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst = -1);
    virtual void update(uint8_t *imageData, UpdateTypes type) override;

  protected:
    virtual void wait();
    virtual void reset();
    virtual void sendCommand(const uint8_t command);
    virtual void sendData(const uint8_t data);
    virtual void sendData(const uint8_t *data, uint32_t size);
    virtual void configFullscreen();     // Select memory region on controller IC
    virtual void configScanning() {}     // Optional. First & last gates, scan direction, etc
    virtual void configVoltages() {}     // Optional. Manual panel voltages, soft-start, etc
    virtual void configWaveform() {}     // Optional. LUT, panel border, temperature sensor, etc
    virtual void configUpdateSequence(); // Tell controller IC which operations to run

    virtual void writeNewImage();
    virtual void writeOldImage();

    virtual void detachFromUpdate();
    virtual bool isUpdateDone() override;
    virtual void finalizeUpdate() override;

  protected:
    uint8_t bufferOffsetX; // In bytes. Panel x=0 does not always align with controller x=0. Quirky internal wiring?
    uint8_t bufferRowSize; // In bytes. Rows store 8 pixels per byte. Rounded up to fit (e.g. 122px would require 16 bytes)
    uint32_t bufferSize;   // In bytes. Rows * Columns
    uint8_t *buffer;
    UpdateTypes updateType;

    uint8_t pin_dc, pin_cs, pin_busy, pin_rst;
    SPIClass *spi;
    SPISettings spiSettings = SPISettings(4000000, MSBFIRST, SPI_MODE0);
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS