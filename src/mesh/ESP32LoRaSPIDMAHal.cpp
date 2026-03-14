#include "configuration.h"
#if defined(ARCH_ESP32) && defined(USE_LORA_SPI_DMA)

#include "ESP32LoRaSPIDMAHal.h"
#include <cstring>
#include <esp_log.h>

#ifndef LORA_SPI_DMA_MAX_TRANSFER
#define LORA_SPI_DMA_MAX_TRANSFER 256
#endif

// Dummy TX buffer for read-only transactions (out == nullptr)
static uint8_t s_dummyTx[LORA_SPI_DMA_MAX_TRANSFER];

ESP32LoRaSPIDMAHal::ESP32LoRaSPIDMAHal(SPIClass &spi, SPISettings spiSettings) : LockingArduinoHal(spi, spiSettings)
{
    _clock_hz = spiSettings._clock;
#if CONFIG_IDF_TARGET_ESP32S3
    _host = SPI2_HOST; // FSPI = default SPI on S3
#elif CONFIG_IDF_TARGET_ESP32
    _host = SPI3_HOST; // VSPI = default SPI on ESP32
#else
    _host = SPI2_HOST;
#endif
}

void ESP32LoRaSPIDMAHal::init()
{
    spiBegin();
    ArduinoHal::init();
}

void ESP32LoRaSPIDMAHal::term()
{
    spiEnd();
    ArduinoHal::term();
}

void ESP32LoRaSPIDMAHal::spiBegin()
{
    if (_bus_initialized) {
        return;
    }
#if defined(LORA_SCK) && defined(LORA_MISO) && defined(LORA_MOSI)
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = LORA_MOSI;
    buscfg.miso_io_num = LORA_MISO;
    buscfg.sclk_io_num = LORA_SCK;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = LORA_SPI_DMA_MAX_TRANSFER;

    esp_err_t ret = spi_bus_initialize(_host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE("LoRa DMA", "spi_bus_initialize failed %d", ret);
        return;
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = (int)_clock_hz;
    devcfg.mode = 0;
    devcfg.spics_io_num = -1; // CS controlled by RadioLib
    devcfg.queue_size = 1;

    ret = spi_bus_add_device(_host, &devcfg, &_handle);
    if (ret != ESP_OK) {
        ESP_LOGE("LoRa DMA", "spi_bus_add_device failed %d", ret);
        spi_bus_free(_host);
        return;
    }
    _bus_initialized = true;
#else
#error "USE_LORA_SPI_DMA requires LORA_SCK, LORA_MISO, LORA_MOSI"
#endif
}

void ESP32LoRaSPIDMAHal::spiBeginTransaction()
{
    LockingArduinoHal::spiBeginTransaction();
}

void ESP32LoRaSPIDMAHal::spiTransfer(uint8_t *out, size_t len, uint8_t *in)
{
    if (!_handle || len == 0) {
        return;
    }
    if (len > LORA_SPI_DMA_MAX_TRANSFER) {
        ESP_LOGE("LoRa DMA", "transfer len %u > max %d", (unsigned)len, LORA_SPI_DMA_MAX_TRANSFER);
        return;
    }

    // Full-duplex transaction. For read-only (out==nullptr) use dummy 0xFF buffer.
    if (out == nullptr) {
        memset(s_dummyTx, 0xFF, len);
        out = s_dummyTx;
    }

    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = out;
    t.rx_buffer = in;

    esp_err_t ret = spi_device_polling_transmit(_handle, &t);
    if (ret != ESP_OK) {
        ESP_LOGE("LoRa DMA", "spi_device_polling_transmit failed %d", ret);
    }
}

void ESP32LoRaSPIDMAHal::spiEndTransaction()
{
    LockingArduinoHal::spiEndTransaction();
}

void ESP32LoRaSPIDMAHal::spiEnd()
{
    if (!_bus_initialized) {
        return;
    }
    if (_handle) {
        spi_bus_remove_device(_handle);
        _handle = nullptr;
    }
    spi_bus_free(_host);
    _bus_initialized = false;
}

#endif // ARCH_ESP32 && USE_LORA_SPI_DMA
