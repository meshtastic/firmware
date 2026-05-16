#pragma once

#include <stddef.h>
#include <stdint.h>

// IEEE 802 Local Experimental EtherType 1, valid on private LANs. Carries a
// Meshtastic RadioBuffer (PacketHeader + payload, ≤255 bytes) end-to-end so the
// LoRa wire format is preserved on HaLow.
static constexpr uint16_t ETHERTYPE_MESHTASTIC_HALOW = 0x88B5;

static constexpr uint8_t HALOW_BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

struct __attribute__((packed)) HaLowEthFrameHeader {
    uint8_t dst[6];
    uint8_t src[6];
    uint16_t ethertype; // network byte order on the wire
};
static_assert(sizeof(HaLowEthFrameHeader) == 14, "HaLow ethernet header must be 14 bytes");
