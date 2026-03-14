#pragma once

#if !MESHTASTIC_EXCLUDE_GPS

#include <cstddef>
#include <cstdint>
#include <cstring>

/**
 * Minimal serial interface used by the GPS layer so we can plug either
 * HardwareSerial or an ESP-IDF UART ring-buffer implementation (e.g. for
 * larger RX buffer / future DMA) without changing the parser.
 */
class IGpsSerial
{
  public:
    virtual ~IGpsSerial() = default;

    virtual int available() = 0;
    virtual int read() = 0;
    virtual size_t readBytes(uint8_t *buffer, size_t length) = 0;
    virtual size_t write(uint8_t c) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size) = 0;
    /** Convenience for string writes (e.g. _serial_gps->write("$PMTK...")). */
    size_t write(const char *str) { return str ? write((const uint8_t *)str, strlen(str)) : 0; }
    virtual void flush() = 0;
    /** txOnly: true = wait for TX idle only; false = also discard RX (clearBuffer). */
    virtual void flush(bool txOnly) = 0;
    virtual void begin(unsigned long baud, uint32_t config, int8_t rxPin, int8_t txPin) = 0;
    virtual void setRxBufferSize(size_t size) = 0;
    virtual void end() = 0;
    virtual uint32_t baudRate() = 0;
    virtual void updateBaudRate(unsigned long baud) = 0;
};

#endif // !MESHTASTIC_EXCLUDE_GPS
