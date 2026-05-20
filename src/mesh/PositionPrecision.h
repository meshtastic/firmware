#pragma once

#include "meshtastic/channel.pb.h"
#include "meshtastic/mesh.pb.h"
#include <stdint.h>

uint32_t getPositionPrecisionForChannel(const meshtastic_Channel &channel);
uint32_t getPositionPrecisionForChannel(uint8_t channelIndex);
void applyPositionPrecision(meshtastic_Position &position, uint32_t precision);
bool applyPositionPrecision(meshtastic_MeshPacket &packet, uint32_t precision);
bool applyPositionPrecisionForChannel(meshtastic_MeshPacket &packet, uint8_t channelIndex);
