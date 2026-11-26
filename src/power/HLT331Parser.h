#pragma once

#include <stddef.h>
#include <stdint.h>

// Status reported by an HLT331 power-bank controller in one serial frame.
struct HLT331Status {
    enum PortState : uint8_t { PORT_NOT_CONNECTED = 0, PORT_CHARGING = 1, PORT_DISCHARGING = 2 };

    bool charging = false;
    uint8_t batteryPercent = 0;
    uint8_t c1State = PORT_NOT_CONNECTED;
    uint8_t c2State = PORT_NOT_CONNECTED;
    bool ntcOverTemp = false;
    bool wirelessActive = false;
    bool wirelessForeignObject = false;
    bool wirelessFull = false;
    uint8_t wirelessProtocol = 0;
    uint8_t fastChargeMode = 0;
    int8_t ntc1TempC = 0;
    int8_t ntc2TempC = 0;
    uint16_t batteryMv = 0;
    uint16_t busMv = 0;
    uint16_t busMa = 0;
    uint32_t wirelessMw = 0;

    // A port feeding the bank counts as external power even when the cell is full and not charging.
    bool inputPresent() const { return charging || c1State == PORT_CHARGING || c2State == PORT_CHARGING; }
};

// Byte-at-a-time decoder for the HLT331 status stream: A5 5A, length 13, 13 data bytes, CRC-8/MAXIM
// over the length and data. The controller only transmits; nothing is ever sent to it.
class HLT331Parser
{
  public:
    static constexpr uint8_t HEADER_0 = 0xA5;
    static constexpr uint8_t HEADER_1 = 0x5A;
    static constexpr uint8_t DATA_LENGTH = 13;

    // Returns true when this byte completes a valid frame; status() then holds its contents.
    bool feed(uint8_t byte);

    const HLT331Status &status() const { return lastStatus; }
    // Data bytes of the last valid frame.
    const uint8_t *lastFrame() const { return lastData; }
    uint32_t goodFrames() const { return goodFrameCount; }
    uint32_t badCrcFrames() const { return badCrcCount; }
    uint32_t badLengthFrames() const { return badLengthCount; }

    static uint8_t crc8(const uint8_t *data, size_t len, uint8_t crc = 0);
    static void decode(const uint8_t data[DATA_LENGTH], HLT331Status &out);

  private:
    enum State : uint8_t { WAIT_HEADER_0, WAIT_HEADER_1, WAIT_LENGTH, READ_DATA, READ_CRC };

    State state = WAIT_HEADER_0;
    uint8_t dataIndex = 0;
    uint8_t data[DATA_LENGTH] = {};
    uint8_t lastData[DATA_LENGTH] = {};
    HLT331Status lastStatus;
    uint32_t goodFrameCount = 0;
    uint32_t badCrcCount = 0;
    uint32_t badLengthCount = 0;
};
