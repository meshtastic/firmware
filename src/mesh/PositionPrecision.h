#pragma once

#include "meshtastic/channel.pb.h"
#include "meshtastic/mesh.pb.h"
#include <stdint.h>

// Max precision on a publicly-decryptable channel. CCPA "precise geolocation" = within a ~564m (1,850ft) radius.
// Precision is bit-truncation of latitude_i/longitude_i: the latitude cell stays ~constant in meters worldwide
// (~700m at 15 bits), while only the longitude cell varies — widest at the equator, narrowing toward the poles.
// 15 also matches the MQTT map-report public precision ceiling.
#define MAX_POSITION_PRECISION_PUBLIC_KEY 15

// Configured precision as-is; does NOT apply the public-key clamp -- use the channelIndex overload for the on-wire value.
uint32_t getPositionPrecisionForChannel(const meshtastic_Channel &channel);

// Configured precision, clamped to MAX_POSITION_PRECISION_PUBLIC_KEY when the channel's effective key is publicly decryptable.
uint32_t getPositionPrecisionForChannel(uint8_t channelIndex);
void applyPositionPrecision(meshtastic_Position &position, uint32_t precision);
bool applyPositionPrecision(meshtastic_MeshPacket &packet, uint32_t precision);
bool applyPositionPrecisionForChannel(meshtastic_MeshPacket &packet, uint8_t channelIndex);
