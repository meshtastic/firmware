#include "PositionPrecision.h"
#include "Channels.h"
#include "mesh-pb-constants.h"

#include <Arduino.h>

uint32_t getPositionPrecisionForChannel(const meshtastic_Channel &channel)
{
    if (channel.settings.has_module_settings) {
        return channel.settings.module_settings.position_precision;
    }
    // No module settings: fail closed. A PRIMARY channel used to default to 32
    // here, leaking an exact position on a sharing-disabled channel. See #10509.
    return 0;
}

uint32_t getPositionPrecisionForChannel(uint8_t channelIndex)
{
    return getPositionPrecisionForChannel(channels.getByIndex(channelIndex));
}

static int32_t truncateCoordinate(int32_t coordinate, uint32_t precision)
{
    uint32_t coordinateBits = static_cast<uint32_t>(coordinate);
    uint32_t truncated = coordinateBits & (UINT32_MAX << (32 - precision));

    // Use the middle of the possible location, not the low edge of the bucket.
    truncated += (1UL << (31 - precision));

    return static_cast<int32_t>(truncated);
}

void applyPositionPrecision(meshtastic_Position &position, uint32_t precision)
{
    if (precision == 0) {
        uint32_t time = position.time;
        position = meshtastic_Position_init_default;
        position.time = time;
        return;
    }

    uint32_t effectivePrecision = precision > 32 ? 32 : precision;
    position.precision_bits = effectivePrecision;

    if (effectivePrecision < 32) {
        position.latitude_i = truncateCoordinate(position.latitude_i, effectivePrecision);
        position.longitude_i = truncateCoordinate(position.longitude_i, effectivePrecision);
    }
}

bool applyPositionPrecision(meshtastic_MeshPacket &packet, uint32_t precision)
{
    if (packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag ||
        packet.decoded.portnum != meshtastic_PortNum_POSITION_APP) {
        return true;
    }

    meshtastic_Position position = meshtastic_Position_init_default;
    if (!pb_decode_from_bytes(packet.decoded.payload.bytes, packet.decoded.payload.size, &meshtastic_Position_msg, &position)) {
        return false;
    }

    applyPositionPrecision(position, precision);
    packet.decoded.payload.size = pb_encode_to_bytes(packet.decoded.payload.bytes, sizeof(packet.decoded.payload.bytes),
                                                     &meshtastic_Position_msg, &position);
    return true;
}

bool applyPositionPrecisionForChannel(meshtastic_MeshPacket &packet, uint8_t channelIndex)
{
    if (packet.which_payload_variant != meshtastic_MeshPacket_decoded_tag ||
        packet.decoded.portnum != meshtastic_PortNum_POSITION_APP) {
        return true;
    }

    return applyPositionPrecision(packet, getPositionPrecisionForChannel(channelIndex));
}
