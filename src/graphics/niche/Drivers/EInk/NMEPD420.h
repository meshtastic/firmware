#pragma once

#ifdef MESHTASTIC_INCLUDE_NICHE_GRAPHICS

#include "configuration.h"

#include "./EInk.h"

namespace NicheGraphics::Drivers
{

class NMEPD420 : public EInk
{
  public:
    NMEPD420();

    void begin(SPIClass *spi, uint8_t pin_dc, uint8_t pin_cs, uint8_t pin_busy, uint8_t pin_rst = -1) override;
    void update(uint8_t *imageData, UpdateTypes type) override;
    bool supports(UpdateTypes type) override;

  protected:
    bool isUpdateDone() override;
    void finalizeUpdate() override;

  private:
    enum class Controller : uint8_t { SSD1683, UC8179 };

    static constexpr uint16_t WIDTH = 400;
    static constexpr uint16_t HEIGHT = 300;
    static constexpr uint32_t BUFFER_SIZE = WIDTH * HEIGHT / 8;
    static constexpr uint32_t UPDATE_TIMEOUT_MS = 30000;

    void detectController();
    void reset();
    bool waitUntilReady(uint32_t timeoutMs, bool markFailure = true);

    void sendCommand(uint8_t command);
    void sendData(uint8_t data);
    void sendData(const uint8_t *data, uint32_t size);
    void sendRepeated(uint8_t value, uint32_t size);

    void configureSsd1683();
    void configureSsdRam();
    void updateSsd1683(UpdateTypes type);
    void finalizeSsd1683();

    void configureUc8179();
    void configureUcRam();
    void updateUc8179();
    void finalizeUc8179();

    Controller controller = Controller::SSD1683;
    uint8_t busyActiveLevel = HIGH;
    uint8_t *buffer = nullptr;
    UpdateTypes updateType = UpdateTypes::FULL;

    uint8_t pinDc = -1;
    uint8_t pinCs = -1;
    uint8_t pinBusy = -1;
    uint8_t pinRst = -1;
    SPIClass *spi = nullptr;
    SPISettings spiSettings = SPISettings(4000000, MSBFIRST, SPI_MODE0);
};

} // namespace NicheGraphics::Drivers

#endif // MESHTASTIC_INCLUDE_NICHE_GRAPHICS
