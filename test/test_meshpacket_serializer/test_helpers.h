#pragma once

#include "serialization/JSON.h"
#include "serialization/MeshPacketSerializer.h"
#include <Arduino.h>
#include <meshtastic/mesh.pb.h>
#include <meshtastic/mqtt.pb.h>
#include <meshtastic/telemetry.pb.h>
#include <pb_decode.h>
#include <pb_encode.h>
#include <unity.h>

// Helper function to create a test packet with the given port and payload
static meshtastic_MeshPacket create_test_packet(meshtastic_PortNum port, const uint8_t *payload, size_t payload_size)
{
    meshtastic_MeshPacket packet = meshtastic_MeshPacket_init_zero;

    packet.id = 0x9999;
    packet.from = 0x11223344;
    packet.to = 0x55667788;
    packet.channel = 0;
    packet.hop_limit = 3;
    packet.want_ack = false;
    packet.priority = meshtastic_MeshPacket_Priority_UNSET;
    packet.rx_time = 1609459200;
    packet.rx_snr = 10.5f;
    packet.hop_start = 3;
    packet.rx_rssi = -85;
    packet.delayed = meshtastic_MeshPacket_Delayed_NO_DELAY;

    // Set decoded variant
    packet.which_payload_variant = meshtastic_MeshPacket_decoded_tag;
    packet.decoded.portnum = port;
    memcpy(packet.decoded.payload.bytes, payload, payload_size);
    packet.decoded.payload.size = payload_size;
    packet.decoded.want_response = false;
    packet.decoded.dest = 0x55667788;
    packet.decoded.source = 0x11223344;
    packet.decoded.request_id = 0;
    packet.decoded.reply_id = 0;
    packet.decoded.emoji = 0;

    return packet;
}
