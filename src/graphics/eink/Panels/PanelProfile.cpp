#include "./PanelProfile.h"

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

using namespace NicheGraphics::Panels;

SPIClass *PanelProfile::beginSpi()
{
#if defined(ARCH_ESP32)
    auto *spi = new SPIClass(HSPI);
#if defined(PIN_EINK_SCLK) && defined(PIN_EINK_MOSI) && defined(PIN_EINK_CS)
    spi->begin(PIN_EINK_SCLK, -1, PIN_EINK_MOSI, PIN_EINK_CS);
#else
    spi->begin();
#endif
    return spi;
#elif defined(ARCH_NRF52)
    SPI1.begin();
    return &SPI1;
#else
    return &SPI;
#endif
}

int8_t PanelProfile::backlightPin() const
{
#ifdef PIN_EINK_EN
    return PIN_EINK_EN;
#else
    return -1;
#endif
}

uint8_t PanelProfile::pinDC() const
{
#ifdef PIN_EINK_DC
    return PIN_EINK_DC;
#else
    return 0xFF;
#endif
}

uint8_t PanelProfile::pinCS() const
{
#ifdef PIN_EINK_CS
    return PIN_EINK_CS;
#else
    return 0xFF;
#endif
}

uint8_t PanelProfile::pinBusy() const
{
#ifdef PIN_EINK_BUSY
    return PIN_EINK_BUSY;
#else
    return 0xFF;
#endif
}

int8_t PanelProfile::pinReset() const
{
#ifdef PIN_EINK_RES
    return PIN_EINK_RES;
#else
    return -1;
#endif
}

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
