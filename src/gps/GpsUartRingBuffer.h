#pragma once

#if !MESHTASTIC_EXCLUDE_GPS && defined(ARCH_ESP32) && defined(USE_GPS_UART_RINGBUF)

#include "IGpsSerial.h"
#include <driver/uart.h>

#ifndef GPS_UART_RINGBUF_RX_SIZE
#define GPS_UART_RINGBUF_RX_SIZE 4096
#endif

/**
 * GPS serial implementation using ESP-IDF UART driver with a large RX ring
 * buffer. Data is received by the driver (interrupt-driven) and consumed by
 * the existing parser via read()/readBytes(). Use when USE_GPS_UART_RINGBUF
 * is defined (e.g. Heltec V4).
 */
class GpsUartRingBuffer : public IGpsSerial
{
  public:
    GpsUartRingBuffer();
    ~GpsUartRingBuffer() override;

    int available() override;
    int read() override;
    size_t readBytes(uint8_t *buffer, size_t length) override;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    void flush() override;
    void flush(bool txOnly) override;
    void begin(unsigned long baud, uint32_t config, int8_t rxPin, int8_t txPin) override;
    void setRxBufferSize(size_t size) override;
    void end() override;
    uint32_t baudRate() override { return _baud; }
    void updateBaudRate(unsigned long baud) override;

  private:
    uart_port_t _uart_num{UART_NUM_1};
    size_t _rx_buffer_size{GPS_UART_RINGBUF_RX_SIZE};
    unsigned long _baud{0};
    bool _initialized{false};
};

#endif // !MESHTASTIC_EXCLUDE_GPS && ARCH_ESP32 && USE_GPS_UART_RINGBUF
