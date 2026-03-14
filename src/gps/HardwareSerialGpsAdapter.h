#pragma once

#if !MESHTASTIC_EXCLUDE_GPS && defined(ARCH_ESP32)

#include "IGpsSerial.h"
#include <HardwareSerial.h>

/**
 * Wraps HardwareSerial so it can be used as IGpsSerial (e.g. when not using
 * the GPS UART ring-buffer path).
 */
class HardwareSerialGpsAdapter : public IGpsSerial
{
  public:
    explicit HardwareSerialGpsAdapter(HardwareSerial *ser) : _serial(ser) {}

    int available() override { return _serial ? _serial->available() : 0; }
    int read() override { return _serial ? _serial->read() : -1; }
    size_t readBytes(uint8_t *buffer, size_t length) override { return _serial ? _serial->readBytes(buffer, length) : 0; }
    size_t write(uint8_t c) override { return _serial ? _serial->write(c) : 0; }
    size_t write(const uint8_t *buffer, size_t size) override { return _serial ? _serial->write(buffer, size) : 0; }
    void flush() override
    {
        if (_serial)
            _serial->flush();
    }
    void flush(bool txOnly) override
    {
        if (_serial)
            _serial->flush(txOnly);
    }
    void begin(unsigned long baud, uint32_t config, int8_t rxPin, int8_t txPin) override
    {
        if (_serial)
            _serial->begin(baud, config, rxPin, txPin);
    }
    void setRxBufferSize(size_t size) override
    {
        if (_serial)
            _serial->setRxBufferSize(size);
    }
    void end() override
    {
        if (_serial)
            _serial->end();
    }
    uint32_t baudRate() override { return _serial ? _serial->baudRate() : 0; }
    void updateBaudRate(unsigned long baud) override
    {
        if (_serial)
            _serial->updateBaudRate(baud);
    }

  private:
    HardwareSerial *_serial;
};

#endif // !MESHTASTIC_EXCLUDE_GPS && ARCH_ESP32
