/*

Parallel-EPD niche driver, backed by FastEPD.

Used for boards with an 8-bit parallel EPD interface (e.g. LILYGO T5 S3 ePaper).
The base class signature passes SPI parameters; this driver ignores them and uses FastEPD
to drive the parallel bus directly.

Gated on NICHE_HAS_FASTEPD because FastEPD is a heavy dependency that only parallel-EPD
variants want pulled in. Variants opt in by defining NICHE_HAS_FASTEPD in their platformio.ini
and adding the FastEPD library to lib_deps.

*/

#pragma once

#include "configuration.h"

#if defined(MESHTASTIC_INCLUDE_NICHE_GRAPHICS) && defined(ARCH_ESP32) && defined(NICHE_HAS_FASTEPD)

#include "./EInk.h"

#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class FASTEPD;

namespace NicheGraphics::Drivers
{

class EInkParallel : public EInk
{
  public:
    EInkParallel(uint16_t width, uint16_t height, uint32_t panelType, uint32_t panelClock = 28000000);
    ~EInkParallel();

    // SPI parameters are unused for parallel panels.
    void begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst = -1) override;
    void update(uint8_t *imageData, UpdateTypes type) override;

    FASTEPD *fastEpd() { return epaper; }

  protected:
    bool isUpdateDone() override;
    void finalizeUpdate() override;

    // Hook for boards that need to bring up GPIO expanders / power pins after FastEPD::initPanel.
    virtual void postPanelInit() {}

  private:
    void copyImageInverted(const uint8_t *src);
    static void asyncFullTask(void *param);

    FASTEPD *epaper = nullptr;
    uint32_t panelType;
    uint32_t panelClock;

    UpdateTypes pendingType = UpdateTypes::UNSPECIFIED;
    std::atomic<bool> asyncRunning{false};
    TaskHandle_t asyncTaskHandle = nullptr;

    uint8_t fastRefreshCount = 0;
    static constexpr uint8_t FULL_SLOW_PERIOD = 100;
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS && ARCH_ESP32 && NICHE_HAS_FASTEPD
