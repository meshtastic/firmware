#pragma once

#include <cstddef>
#include <cstdint>

/// Canonical framing for the StreamAPI wire protocol.
///
/// Every frame is START1 <discriminator> LEN_H LEN_L [payload], where the second byte selects
/// between a normal ToRadio/FromRadio frame (START2) and a SerialHal command/response frame
/// (SERIALHAL_MAGIC). Three translation units parse or emit these bytes - StreamAPI (the receive
/// state machine), SerialHalDevice (the device-side responder) and SerialHal (the Portduino-side
/// host) - so they are defined here once. Request and response framing desyncing because one copy
/// drifted is a failure mode that is very hard to spot on the wire.

constexpr uint8_t START1 = 0x94;
constexpr uint8_t START2 = 0xc3;
constexpr uint8_t SERIALHAL_MAGIC = 0xa5;
constexpr size_t HEADER_LEN = 4;
