#pragma once

#include <climits>

#include "mesh/generated/meshtastic/config.pb.h"

namespace GpsProfile
{
inline constexpr uint32_t basePositionFlags()
{
    return meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE | meshtastic_Config_PositionConfig_PositionFlags_DOP |
           meshtastic_Config_PositionConfig_PositionFlags_SATINVIEW | meshtastic_Config_PositionConfig_PositionFlags_TIMESTAMP;
}

inline void applyFixed(meshtastic_Config_PositionConfig &position)
{
    position.gps_update_interval = 1800;
    position.position_broadcast_secs = 3600;
    position.broadcast_smart_minimum_interval_secs = INT32_MAX;
    position.broadcast_smart_minimum_distance = INT32_MAX;
    position.position_flags = basePositionFlags();
    position.position_broadcast_smart_enabled = false;
}

inline bool apply(meshtastic_Config_PositionConfig &position)
{
    switch (position.gps_profile) {
    case meshtastic_Config_PositionConfig_GpsProfile_FIXED_POSITION:
        applyFixed(position);
        return true;
    case meshtastic_Config_PositionConfig_GpsProfile_PEDESTRIAN:
        position.gps_update_interval = 60;
        position.position_broadcast_secs = 900;
        position.broadcast_smart_minimum_interval_secs = 60;
        position.broadcast_smart_minimum_distance = 50;
        position.position_flags = basePositionFlags() | meshtastic_Config_PositionConfig_PositionFlags_SPEED |
                                  meshtastic_Config_PositionConfig_PositionFlags_HEADING;
        position.position_broadcast_smart_enabled = true;
        return true;
    case meshtastic_Config_PositionConfig_GpsProfile_VEHICLE:
        position.gps_update_interval = 30;
        position.position_broadcast_secs = 900;
        position.broadcast_smart_minimum_interval_secs = 120;
        position.broadcast_smart_minimum_distance = 500;
        position.position_flags = basePositionFlags() | meshtastic_Config_PositionConfig_PositionFlags_SPEED |
                                  meshtastic_Config_PositionConfig_PositionFlags_HEADING;
        position.position_broadcast_smart_enabled = true;
        return true;
    case meshtastic_Config_PositionConfig_GpsProfile_AIRBORNE:
        position.gps_update_interval = 10;
        position.position_broadcast_secs = 900;
        position.broadcast_smart_minimum_interval_secs = 60;
        position.broadcast_smart_minimum_distance = 2000;
        position.position_flags = basePositionFlags() | meshtastic_Config_PositionConfig_PositionFlags_ALTITUDE_MSL |
                                  meshtastic_Config_PositionConfig_PositionFlags_GEOIDAL_SEPARATION |
                                  meshtastic_Config_PositionConfig_PositionFlags_SPEED |
                                  meshtastic_Config_PositionConfig_PositionFlags_HEADING;
        position.position_broadcast_smart_enabled = true;
        return true;
    case meshtastic_Config_PositionConfig_GpsProfile_MANUAL:
    default:
        return false;
    }
}

inline bool ubxDynamicModel(meshtastic_Config_PositionConfig_GpsProfile profile, uint8_t &dynamicModel)
{
    switch (profile) {
    case meshtastic_Config_PositionConfig_GpsProfile_FIXED_POSITION:
        dynamicModel = 2;
        return true;
    case meshtastic_Config_PositionConfig_GpsProfile_PEDESTRIAN:
        dynamicModel = 3;
        return true;
    case meshtastic_Config_PositionConfig_GpsProfile_VEHICLE:
        dynamicModel = 4;
        return true;
    case meshtastic_Config_PositionConfig_GpsProfile_AIRBORNE:
        dynamicModel = 7;
        return true;
    case meshtastic_Config_PositionConfig_GpsProfile_MANUAL:
    default:
        return false;
    }
}
} // namespace GpsProfile
