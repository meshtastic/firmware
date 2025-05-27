#include "TypeConversions.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

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