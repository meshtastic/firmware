#pragma once

#ifndef FAKEUART_H
#define FAKEUART_H

#ifdef SENSECAP_INDICATOR

#include "../IndicatorSerial.h"
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
    void flush(bool wait);
    // Stream/Print contract: flush() through a base pointer must behave
    // like the HardwareSerial variant callers expect
    void flush() override { flush(true); }
    // writes are buffered into one uplink message, so this is its capacity
    // (Print's default of 0 would make drivers back off forever)
    int availableForWrite() override { return (int)sizeof(meshtastic_InterdeviceMessage{}.data.nmea) - 1; }
    uint32_t baudRate();
    void updateBaudRate(unsigned long speed);
    size_t setRxBufferSize(size_t size);
    size_t write(const char *buffer);
    size_t write(char *buffer, size_t size);
    size_t write(uint8_t *buffer, size_t size);
    size_t write(const uint8_t *buffer, size_t size) override { return write((char *)buffer, size); }

    size_t stuff_buffer(const char *buffer, size_t size);
    size_t write(uint8_t c) override { return write(&c, 1); }

  private:
    unsigned long baudrate = 115200;

    // Self-contained single-producer/single-consumer byte ring buffer.
    // (An external RingBuf.h is ambiguous in this build: SdFat ships a
    // conflicting header via device-ui.)
    static const size_t BUF_SIZE = 2048;
    uint8_t buf[BUF_SIZE];
    volatile size_t buf_head = 0; // write index (producer)
    volatile size_t buf_tail = 0; // read index (consumer)

    bool buf_push(uint8_t c)
    {
        size_t next = (buf_head + 1) % BUF_SIZE;
        if (next == buf_tail)
            return false; // full
        buf[buf_head] = c;
        // producer and consumer run on different cores: the byte must be
        // visible before the index says it is there
        __sync_synchronize();
        buf_head = next;
        return true;
    }
    void buf_clear() { buf_tail = buf_head; }
    size_t buf_avail() { return (buf_head + BUF_SIZE - buf_tail) % BUF_SIZE; }
};

extern FakeUART *FakeSerial;

#endif // SENSECAP_INDICATOR

#endif // FAKEUART_H