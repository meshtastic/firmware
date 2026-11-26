#pragma once
// A scripted HardwareSerial for the native GPS reply-parser tests. Stream::readBytes() uses
// read(), so UBX/CAS frames can be queued as raw bytes.
#include <HardwareSerial.h>
#include <cstdint>
#include <vector>

class FakeGpsSerial : public HardwareSerial
{
  public:
    void begin(unsigned long) override {}
    void begin(unsigned long, uint16_t) override {}
    void end() override {}
    int available() override { return (int)(bytes.size() - pos); }
    int peek() override { return available() ? bytes[pos] : -1; }
    int read() override { return available() ? bytes[pos++] : -1; }
    void flush() override {}
    size_t write(uint8_t) override { return 1; } // commands are discarded, never parsed back
    operator bool() override { return true; }

    void feed(const std::vector<uint8_t> &frame)
    {
        bytes = frame;
        pos = 0;
    }

  private:
    std::vector<uint8_t> bytes;
    size_t pos = 0;
};
