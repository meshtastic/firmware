//E-Ink base class for displays based on UC8175 / UC8176 style controller ICs.

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./EInk.h"

namespace NicheGraphics::Drivers
{

class UC8175 : public EInk
{
  public:
    UC8175(uint16_t width, uint16_t height, UpdateTypes supported);
    void begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst = -1) override;
    void update(uint8_t *imageData, UpdateTypes type) override;

  protected:
    virtual void wait(uint32_t timeoutMs = 1000);
    virtual void reset();
    virtual void sendCommand(uint8_t command);
    virtual void sendData(uint8_t data);
    virtual void sendData(const uint8_t *data, uint32_t size);

    virtual void configCommon() = 0; // Always run
    virtual void configFull() = 0;   // Run when updateType == FULL
    virtual void configFast() = 0;   // Run when updateType == FAST

    virtual void powerOn();
    virtual void powerOff();
    virtual void writeOldImage();
    virtual void writeNewImage();
    virtual void writeImage(uint8_t command, const uint8_t *image);

    virtual void detachFromUpdate();
    virtual bool isUpdateDone() override;
    virtual void finalizeUpdate() override;

  protected:
    static constexpr uint8_t BUSY_ACTIVE = LOW;

    uint16_t bufferRowSize = 0;
    uint32_t bufferSize = 0;
    uint8_t *buffer = nullptr;
    uint8_t *previousBuffer = nullptr;
    bool hasPreviousBuffer = false;
    UpdateTypes updateType = UpdateTypes::UNSPECIFIED;

    uint8_t pin_dc = (uint8_t)-1;
    uint8_t pin_cs = (uint8_t)-1;
    uint8_t pin_busy = (uint8_t)-1;
    uint8_t pin_rst = (uint8_t)-1;
    SPIClass *spi = nullptr;
    SPISettings spiSettings = SPISettings(8000000, MSBFIRST, SPI_MODE0);
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
