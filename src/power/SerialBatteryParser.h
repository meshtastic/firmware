#pragma once

#include <stdint.h>

// Battery state reported by the ThinkNode M4's power-bank MCU in one serial frame.
struct SerialBatteryStatus {
    uint8_t batteryPercent = 0;
    uint16_t batteryMv = 0;
};

// Byte-at-a-time decoder for the ThinkNode M4 power-bank stream: FE, percent, voltage as three base-100
// digit pairs (volts, then two pairs of decimals) of half the pack voltage, FD. No checksum.
class SerialBatteryParser
{
  public:
    static constexpr uint8_t START = 0xFE;
    static constexpr uint8_t END = 0xFD;
    static constexpr uint8_t PAYLOAD_LENGTH = 4;

    // Returns true when this byte completes a valid frame; status() then holds its contents.
    bool feed(uint8_t byte);

    const SerialBatteryStatus &status() const { return lastStatus; }
    uint32_t goodFrames() const { return goodFrameCount; }
    uint32_t badFrames() const { return badFrameCount; }

  private:
    bool inFrame = false;
    uint8_t payloadIndex = 0;
    uint8_t payload[PAYLOAD_LENGTH] = {};
    SerialBatteryStatus lastStatus;
    uint32_t goodFrameCount = 0;
    uint32_t badFrameCount = 0;
};
