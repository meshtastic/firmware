#pragma once

#ifndef FAKEUART_H
#define FAKEUART_H

#ifdef SENSECAP_INDICATOR

#include "../IndicatorSerial.h"
#include <RingBuf.h>
#include <Stream.h>
#include <inttypes.h>

class FakeUART : public Stream
{
  public:
    FakeUART();

    void begin(unsigned long baud, uint32_t config = 0x800001c, int8_t rxPin = -1, int8_t txPin = -1, bool invert = false,
               unsigned long timeout_ms = 20000UL, uint8_t rxfifo_full_thrhd = 112);
    void end();
    int available();
    int peek();
    int read();
    void flush(bool wait = true);
    uint32_t baudRate();
    void updateBaudRate(unsigned long speed);
    size_t setRxBufferSize(size_t size);
    size_t write(const char *buffer);
    size_t write(char *buffer, size_t size);
    size_t write(uint8_t *buffer, size_t size);

    size_t stuff_buffer(const char *buffer, size_t size);
    virtual size_t write(uint8_t c) { return write(&c, 1); }

  private:
    unsigned long baudrate = 115200;
    RingBuf<unsigned char, 2048> FakeBuf;
};

extern FakeUART *FakeSerial;

#endif // SENSECAP_INDICATOR

#endif // FAKEUART_H