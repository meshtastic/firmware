#include "configuration.h"
#if !MESHTASTIC_EXCLUDE_GPS && defined(ARCH_ESP32) && defined(USE_GPS_UART_RINGBUF)

#include "GpsUartRingBuffer.h"
#include "configuration.h"
#include <driver/uart.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "GpsUart";

GpsUartRingBuffer::GpsUartRingBuffer() {}

GpsUartRingBuffer::~GpsUartRingBuffer()
{
    end();
}

void GpsUartRingBuffer::setRxBufferSize(size_t size)
{
    if (!_initialized) {
        _rx_buffer_size = size;
    }
}

void GpsUartRingBuffer::begin(unsigned long baud, uint32_t config, int8_t rxPin, int8_t txPin)
{
    if (_initialized) {
        uart_set_baudrate(_uart_num, baud);
        _baud = baud;
        return;
    }

    const int tx_buffer_size = 256;
    esp_err_t err = uart_driver_install(_uart_num, (int)_rx_buffer_size, tx_buffer_size, 0, nullptr, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed %d", err);
        return;
    }

    uart_config_t uart_config = {};
    uart_config.baud_rate = (int)baud;
    uart_config.data_bits = (uart_word_length_t)((config & 0xc) >> 2);
    uart_config.parity = (uart_parity_t)(config & 0x3);
    uart_config.stop_bits = (uart_stop_bits_t)((config & 0x30) >> 4);
    uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
#if SOC_UART_SUPPORT_XTAL_CLK
    uart_config.source_clk = UART_SCLK_XTAL;
#else
    uart_config.source_clk = UART_SCLK_APB;
#endif

    err = uart_param_config(_uart_num, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed %d", err);
        uart_driver_delete(_uart_num);
        return;
    }

    if (rxPin >= 0 && txPin >= 0) {
        err = uart_set_pin(_uart_num, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "uart_set_pin failed %d", err);
        }
    }

    _baud = baud;
    _initialized = true;
}

void GpsUartRingBuffer::end()
{
    if (!_initialized) {
        return;
    }
    uart_driver_delete(_uart_num);
    _initialized = false;
}

int GpsUartRingBuffer::available()
{
    if (!_initialized) {
        return 0;
    }
    size_t len = 0;
    esp_err_t err = uart_get_buffered_data_len(_uart_num, &len);
    return (err == ESP_OK) ? (int)len : 0;
}

int GpsUartRingBuffer::read()
{
    if (!_initialized) {
        return -1;
    }
    uint8_t c = 0;
    int n = uart_read_bytes(_uart_num, &c, 1, 0);
    return (n == 1) ? (int)c : -1;
}

size_t GpsUartRingBuffer::readBytes(uint8_t *buffer, size_t length)
{
    if (!_initialized || buffer == nullptr) {
        return 0;
    }
    int n = uart_read_bytes(_uart_num, buffer, length, pdMS_TO_TICKS(20));
    return (n > 0) ? (size_t)n : 0;
}

size_t GpsUartRingBuffer::write(uint8_t c)
{
    if (!_initialized) {
        return 0;
    }
    int n = uart_write_bytes(_uart_num, &c, 1);
    return (n == 1) ? 1 : 0;
}

size_t GpsUartRingBuffer::write(const uint8_t *buffer, size_t size)
{
    if (!_initialized || buffer == nullptr) {
        return 0;
    }
    int n = uart_write_bytes(_uart_num, buffer, size);
    return (n > 0) ? (size_t)n : 0;
}

void GpsUartRingBuffer::flush()
{
    if (_initialized) {
        uart_wait_tx_done(_uart_num, pdMS_TO_TICKS(100));
    }
}

void GpsUartRingBuffer::flush(bool txOnly)
{
    if (!_initialized) {
        return;
    }
    if (txOnly) {
        uart_wait_tx_done(_uart_num, pdMS_TO_TICKS(100));
    } else {
        uart_flush_input(_uart_num);
    }
}

void GpsUartRingBuffer::updateBaudRate(unsigned long baud)
{
    if (_initialized) {
        uart_set_baudrate(_uart_num, baud);
        _baud = baud;
    }
}

#endif // !MESHTASTIC_EXCLUDE_GPS && ARCH_ESP32 && USE_GPS_UART_RINGBUF
