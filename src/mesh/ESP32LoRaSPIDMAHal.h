#pragma once

#if defined(ARCH_ESP32) && defined(USE_LORA_SPI_DMA)

#include "RadioLibInterface.h"
#include <SPI.h>
#include <driver/spi_master.h>
#include <esp_err.h>

/**
 * RadioLib HAL that uses ESP-IDF SPI master with DMA for LoRa SPI transfers.
 * Use when USE_LORA_SPI_DMA is defined (e.g. Heltec V4). Replaces the default
 * byte-by-byte Arduino SPI path so transfers use GDMA.
 */
class ESP32LoRaSPIDMAHal : public LockingArduinoHal
{
  public:
    ESP32LoRaSPIDMAHal(SPIClass &spi, SPISettings spiSettings);

    void init() override;
    void term() override;
    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t *out, size_t len, uint8_t *in) override;
    void spiEndTransaction() override;
    void spiEnd() override;

  private:
    spi_host_device_t _host = SPI2_HOST;
    spi_device_handle_t _handle = nullptr;
    bool _bus_initialized = false;
    uint32_t _clock_hz = 4000000;
};

#endif // ARCH_ESP32 && USE_LORA_SPI_DMA
