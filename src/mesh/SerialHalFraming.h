#pragma once

#include <cstdint>

// Wire constants shared by the SerialHal host (platform/portduino/SerialHal) and device (mesh/SerialHalDevice).
// Frames are START1 MAGIC LEN_H LEN_L [protobuf payload], interleaved with normal START1 START2 StreamAPI frames.
namespace serialhal
{
constexpr uint8_t FRAME_START1 = 0x94;
constexpr uint8_t FRAME_START2 = 0xc3; // second byte of a normal ToRadio/FromRadio frame
constexpr uint8_t FRAME_MAGIC = 0xa5;  // second byte of a SerialHal frame
constexpr uint8_t FRAME_HEADER_LEN = 4;

// transaction_id 0 is reserved for unsolicited interrupt events from the device.
constexpr uint16_t INTERRUPT_TRANSACTION_ID = 0;

// Pin mode / interrupt edge values carried in SerialHalCommand.mode.
constexpr uint32_t PIN_INPUT = 0;
constexpr uint32_t PIN_OUTPUT = 1;
constexpr uint32_t PIN_LOW = 0;
constexpr uint32_t PIN_HIGH = 1;
constexpr uint32_t EDGE_RISING = 1;
constexpr uint32_t EDGE_FALLING = 2;
} // namespace serialhal
