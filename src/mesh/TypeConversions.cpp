#include "TypeConversions.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include <algorithm>
#include <cmath>
#include <cstring>

meshtastic_NodeInfo TypeConversions::ConvertToNodeInfo(const meshtastic_NodeInfoLite *lite)
{
    meshtastic_NodeInfo info = meshtastic_NodeInfo_init_default;

    info.num = lite->num;
    info.snr = lite->snr;
    info.last_heard = lite->last_heard;
    info.channel = lite->channel;
    info.via_mqtt = lite->via_mqtt;
    info.is_favorite = lite->is_favorite;
    info.is_ignored = lite->is_ignored;
    info.is_key_manually_verified = lite->bitfield & NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK;

    if (lite->has_hops_away) {
        info.has_hops_away = true;
        info.hops_away = lite->hops_away;
    }

    if (lite->has_position) {
        info.has_position = true;
        if (lite->position.latitude_i != 0)
            info.position.has_latitude_i = true;
        info.position.latitude_i = lite->position.latitude_i;
        if (lite->position.longitude_i != 0)
            info.position.has_longitude_i = true;
        info.position.longitude_i = lite->position.longitude_i;
        if (lite->position.altitude != 0)
            info.position.has_altitude = true;
        info.position.altitude = lite->position.altitude;
        info.position.location_source = lite->position.location_source;
        info.position.time = lite->position.time;
    }
    if (lite->has_user) {
        info.has_user = true;
        info.user = ConvertToUser(lite->num, lite->user);
    }
    if (lite->has_device_metrics) {
        info.has_device_metrics = true;
        info.device_metrics = lite->device_metrics;
    }
    return info;
}

meshtastic_NodeInfo TypeConversions::ConvertToNodeInfo(const meshtastic_NodeDetail &detail)
{
    meshtastic_NodeInfo info = meshtastic_NodeInfo_init_default;

    info.num = detail.num;
    info.snr = detail.snr;
    info.last_heard = detail.last_heard;
    info.channel = detail.channel;
    info.via_mqtt = detail.flags & NODEDETAIL_FLAG_VIA_MQTT;
    info.is_favorite = detail.flags & NODEDETAIL_FLAG_IS_FAVORITE;
    info.is_ignored = detail.flags & NODEDETAIL_FLAG_IS_IGNORED;
    info.is_key_manually_verified = detail.flags & NODEDETAIL_FLAG_IS_KEY_MANUALLY_VERIFIED;

    if (detail.flags & NODEDETAIL_FLAG_HAS_HOPS_AWAY) {
        info.has_hops_away = true;
        info.hops_away = detail.hops_away;
    }

    if (detail.flags & NODEDETAIL_FLAG_HAS_POSITION) {
        info.has_position = true;
        info.position = meshtastic_Position_init_default;
        if (detail.latitude_i != 0) {
            info.position.has_latitude_i = true;
        }
        info.position.latitude_i = detail.latitude_i;
        if (detail.longitude_i != 0) {
            info.position.has_longitude_i = true;
        }
        info.position.longitude_i = detail.longitude_i;
        if (detail.altitude != 0) {
            info.position.has_altitude = true;
        }
        info.position.altitude = detail.altitude;
        info.position.location_source = detail.position_source;
        info.position.time = detail.position_time;
    }

    if (detail.flags & NODEDETAIL_FLAG_HAS_USER) {
        info.has_user = true;
        meshtastic_User user = meshtastic_User_init_default;
        snprintf(user.id, sizeof(user.id), "!%08x", detail.num);
        strncpy(user.long_name, detail.long_name, sizeof(user.long_name));
        user.long_name[sizeof(user.long_name) - 1] = '\0';
        strncpy(user.short_name, detail.short_name, sizeof(user.short_name));
        user.short_name[sizeof(user.short_name) - 1] = '\0';
        user.hw_model = detail.hw_model;
        user.role = detail.role;
        user.is_licensed = detail.flags & NODEDETAIL_FLAG_IS_LICENSED;
        memcpy(user.macaddr, detail.macaddr, sizeof(user.macaddr));
        const pb_size_t keySize = std::min(detail.public_key.size, static_cast<pb_size_t>(sizeof(user.public_key.bytes)));
        memcpy(user.public_key.bytes, detail.public_key.bytes, keySize);
        user.public_key.size = keySize;
        if (detail.flags & NODEDETAIL_FLAG_HAS_UNMESSAGABLE) {
            user.has_is_unmessagable = true;
            user.is_unmessagable = detail.flags & NODEDETAIL_FLAG_IS_UNMESSAGABLE;
        }
        info.user = user;
    }

    if (detail.flags & NODEDETAIL_FLAG_HAS_DEVICE_METRICS) {
        info.has_device_metrics = true;
        meshtastic_DeviceMetrics metrics = meshtastic_DeviceMetrics_init_default;

        if (detail.flags & NODEDETAIL_FLAG_HAS_BATTERY_LEVEL) {
            metrics.has_battery_level = true;
            metrics.battery_level = detail.battery_level;
        }
        if (detail.flags & NODEDETAIL_FLAG_HAS_VOLTAGE) {
            metrics.has_voltage = true;
            metrics.voltage = static_cast<float>(detail.voltage_millivolts) / 1000.0f;
        }
        if (detail.flags & NODEDETAIL_FLAG_HAS_CHANNEL_UTIL) {
            metrics.has_channel_utilization = true;
            metrics.channel_utilization = static_cast<float>(detail.channel_utilization_permille) / 10.0f;
        }
        if (detail.flags & NODEDETAIL_FLAG_HAS_AIR_UTIL_TX) {
            metrics.has_air_util_tx = true;
            metrics.air_util_tx = static_cast<float>(detail.air_util_tx_permille) / 10.0f;
        }
        if (detail.flags & NODEDETAIL_FLAG_HAS_UPTIME) {
            metrics.has_uptime_seconds = true;
            metrics.uptime_seconds = detail.uptime_seconds;
        }

        info.device_metrics = metrics;
    }

    return info;
}

meshtastic_NodeDetail TypeConversions::ConvertToNodeDetail(const meshtastic_NodeInfoLite &lite)
{
    meshtastic_NodeDetail detail = meshtastic_NodeDetail_init_default;

    detail.num = lite.num;
    detail.snr = lite.snr;
    detail.last_heard = lite.last_heard;
    detail.channel = lite.channel;
    detail.next_hop = lite.next_hop;
    if (lite.has_hops_away) {
        detail.flags |= NODEDETAIL_FLAG_HAS_HOPS_AWAY;
        detail.hops_away = lite.hops_away;
    }
    if (lite.via_mqtt) {
        detail.flags |= NODEDETAIL_FLAG_VIA_MQTT;
    }
    if (lite.is_favorite) {
        detail.flags |= NODEDETAIL_FLAG_IS_FAVORITE;
    }
    if (lite.is_ignored) {
        detail.flags |= NODEDETAIL_FLAG_IS_IGNORED;
    }
    if (lite.bitfield & NODEINFO_BITFIELD_IS_KEY_MANUALLY_VERIFIED_MASK) {
        detail.flags |= NODEDETAIL_FLAG_IS_KEY_MANUALLY_VERIFIED;
    }

    if (lite.has_user) {
        detail.flags |= NODEDETAIL_FLAG_HAS_USER;
        strncpy(detail.long_name, lite.user.long_name, sizeof(detail.long_name));
        detail.long_name[sizeof(detail.long_name) - 1] = '\0';
        strncpy(detail.short_name, lite.user.short_name, sizeof(detail.short_name));
        detail.short_name[sizeof(detail.short_name) - 1] = '\0';
        detail.hw_model = lite.user.hw_model;
        detail.role = lite.user.role;
        memcpy(detail.macaddr, lite.user.macaddr, sizeof(detail.macaddr));
        const pb_size_t keySize = std::min(lite.user.public_key.size, static_cast<pb_size_t>(sizeof(detail.public_key.bytes)));
        memcpy(detail.public_key.bytes, lite.user.public_key.bytes, keySize);
        detail.public_key.size = keySize;
        if (lite.user.is_licensed) {
            detail.flags |= NODEDETAIL_FLAG_IS_LICENSED;
        }
        if (lite.user.has_is_unmessagable) {
            detail.flags |= NODEDETAIL_FLAG_HAS_UNMESSAGABLE;
            if (lite.user.is_unmessagable) {
                detail.flags |= NODEDETAIL_FLAG_IS_UNMESSAGABLE;
            }
        }
    }

    if (lite.has_position) {
        detail.flags |= NODEDETAIL_FLAG_HAS_POSITION;
        detail.latitude_i = lite.position.latitude_i;
        detail.longitude_i = lite.position.longitude_i;
        detail.altitude = lite.position.altitude;
        detail.position_time = lite.position.time;
        detail.position_source = lite.position.location_source;
    }

    if (lite.has_device_metrics) {
        detail.flags |= NODEDETAIL_FLAG_HAS_DEVICE_METRICS;
        const meshtastic_DeviceMetrics &metrics = lite.device_metrics;

        if (metrics.has_battery_level) {
            detail.flags |= NODEDETAIL_FLAG_HAS_BATTERY_LEVEL;
            uint32_t battery = metrics.battery_level;
            if (battery > 255u) {
                battery = 255u;
            }
            detail.battery_level = static_cast<uint8_t>(battery);
        }
        if (metrics.has_voltage) {
            detail.flags |= NODEDETAIL_FLAG_HAS_VOLTAGE;
            double limitedVoltage = clampValue(static_cast<double>(metrics.voltage), 0.0, 65.535);
            int millivolts = static_cast<int>(std::lround(limitedVoltage * 1000.0));
            millivolts = clampValue<int>(millivolts, 0, 0xFFFF);
            detail.voltage_millivolts = static_cast<uint16_t>(millivolts);
        }
        if (metrics.has_channel_utilization) {
            detail.flags |= NODEDETAIL_FLAG_HAS_CHANNEL_UTIL;
            double limitedUtil = clampValue(static_cast<double>(metrics.channel_utilization), 0.0, 100.0);
            int permille = static_cast<int>(std::lround(limitedUtil * 10.0));
            permille = clampValue<int>(permille, 0, 1000);
            detail.channel_utilization_permille = static_cast<uint16_t>(permille);
        }
        if (metrics.has_air_util_tx) {
            detail.flags |= NODEDETAIL_FLAG_HAS_AIR_UTIL_TX;
            double limitedAirUtil = clampValue(static_cast<double>(metrics.air_util_tx), 0.0, 100.0);
            int permille = static_cast<int>(std::lround(limitedAirUtil * 10.0));
            permille = clampValue<int>(permille, 0, 1000);
            detail.air_util_tx_permille = static_cast<uint16_t>(permille);
        }
        if (metrics.has_uptime_seconds) {
            detail.flags |= NODEDETAIL_FLAG_HAS_UPTIME;
            detail.uptime_seconds = metrics.uptime_seconds;
        }
    }

    return detail;
}

meshtastic_PositionLite TypeConversions::ConvertToPositionLite(meshtastic_Position position)
{
    meshtastic_PositionLite lite = meshtastic_PositionLite_init_default;
    lite.latitude_i = position.latitude_i;
    lite.longitude_i = position.longitude_i;
    lite.altitude = position.altitude;
    lite.location_source = position.location_source;
    lite.time = position.time;

    return lite;
}

meshtastic_Position TypeConversions::ConvertToPosition(meshtastic_PositionLite lite)
{
    meshtastic_Position position = meshtastic_Position_init_default;
    if (lite.latitude_i != 0)
        position.has_latitude_i = true;
    position.latitude_i = lite.latitude_i;
    if (lite.longitude_i != 0)
        position.has_longitude_i = true;
    position.longitude_i = lite.longitude_i;
    if (lite.altitude != 0)
        position.has_altitude = true;
    position.altitude = lite.altitude;
    position.location_source = lite.location_source;
    position.time = lite.time;

    return position;
}

meshtastic_UserLite TypeConversions::ConvertToUserLite(meshtastic_User user)
{
    meshtastic_UserLite lite = meshtastic_UserLite_init_default;

    strncpy(lite.long_name, user.long_name, sizeof(lite.long_name));
    strncpy(lite.short_name, user.short_name, sizeof(lite.short_name));
    lite.hw_model = user.hw_model;
    lite.role = user.role;
    lite.is_licensed = user.is_licensed;
    memcpy(lite.macaddr, user.macaddr, sizeof(lite.macaddr));
    memcpy(lite.public_key.bytes, user.public_key.bytes, sizeof(lite.public_key.bytes));
    lite.public_key.size = user.public_key.size;
    lite.has_is_unmessagable = user.has_is_unmessagable;
    lite.is_unmessagable = user.is_unmessagable;
    return lite;
}

meshtastic_User TypeConversions::ConvertToUser(uint32_t nodeNum, meshtastic_UserLite lite)
{
    meshtastic_User user = meshtastic_User_init_default;

    snprintf(user.id, sizeof(user.id), "!%08x", nodeNum);
    strncpy(user.long_name, lite.long_name, sizeof(user.long_name));
    strncpy(user.short_name, lite.short_name, sizeof(user.short_name));
    user.hw_model = lite.hw_model;
    user.role = lite.role;
    user.is_licensed = lite.is_licensed;
    memcpy(user.macaddr, lite.macaddr, sizeof(user.macaddr));
    memcpy(user.public_key.bytes, lite.public_key.bytes, sizeof(user.public_key.bytes));
    user.public_key.size = lite.public_key.size;
    user.has_is_unmessagable = lite.has_is_unmessagable;
    user.is_unmessagable = lite.is_unmessagable;

    return user;
}