/*

E-Ink display driver
    - GDEW0102T4
    - Controller: UC8175
    - Size: 1.02 inch
    - Resolution: 80px x 128px

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./EInk.h"

namespace NicheGraphics::Drivers
{

class GDEW0102T4 : public EInk
{
  private:
    static constexpr uint16_t width = 80;
    static constexpr uint16_t height = 128;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    GDEW0102T4();
    void begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst = -1) override;
    void update(uint8_t *imageData, UpdateTypes type) override;

  protected:
    void wait(uint32_t timeoutMs = 1000);
    void reset();
    void sendCommand(uint8_t command);
    void sendData(uint8_t data);
    void sendData(const uint8_t *data, uint32_t size);
    void configDisplay();
    void writeOldImage();
    void writeNewImage();

    void detachFromUpdate();
    bool isUpdateDone() override;
    void finalizeUpdate() override;

  private:
    static constexpr uint8_t BUSY_ACTIVE = LOW;

    uint16_t bufferRowSize = 0;
    uint32_t bufferSize = 0;
    uint8_t *buffer = nullptr;
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
