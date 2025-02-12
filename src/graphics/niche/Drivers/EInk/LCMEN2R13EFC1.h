/*

E-Ink display driver
    - LCMEN213EFC1
    - Manufacturer: Wisevast
    - Size: 2.13 inch
    - Resolution: 122px x 250px
    - Flex connector marking: HINK-E0213A162-FPC-A0 (Hidden, printed on back-side)

Note: this display uses an uncommon controller IC, Fitipower JD79656.
It is implemented as a "one-off", directly inheriting the EInk base class, unlike SSD16XX displays.

*/

#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./EInk.h"

namespace NicheGraphics::Drivers
{

class LCMEN213EFC1 : public EInk
{
    // Display properties
  private:
    static constexpr uint32_t width = 122;
    static constexpr uint32_t height = 250;
    static constexpr UpdateTypes supported = (UpdateTypes)(FULL | FAST);

  public:
    LCMEN213EFC1();
    void begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst);
    void update(uint8_t *imageData, UpdateTypes type) override;

  protected:
    void wait();
    void reset();
    void sendCommand(const uint8_t command);
    void sendData(const uint8_t data);
    void sendData(const uint8_t *data, uint32_t size);
    void configFull(); // Configure display for FULL refresh
    void configFast(); // Configure display for FAST refresh
    void writeNewImage();
    void writeOldImage();

    void detachFromUpdate();
    bool isUpdateDone();
    void finalizeUpdate();

  protected:
    uint8_t bufferOffsetX; // In bytes. Panel x=0 does not always align with controller x=0. Quirky internal wiring?
    uint8_t bufferRowSize; // In bytes. Rows store 8 pixels per byte. Rounded up to fit (e.g. 122px would require 16 bytes)
    uint32_t bufferSize;   // In bytes. Rows * Columns
    uint8_t *buffer;
    UpdateTypes updateType;

    uint8_t pin_dc, pin_cs, pin_busy, pin_rst;
    SPIClass *spi;
    SPISettings spiSettings = SPISettings(6000000, MSBFIRST, SPI_MODE0);
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS