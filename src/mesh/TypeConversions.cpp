#include "TypeConversions.h"
#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "meshUtils.h"

meshtastic_NodeInfo TypeConversions::ConvertToNodeInfo(const meshtastic_NodeInfoLite *lite,
                                                       const meshtastic_PositionLite *position,
                                                       const meshtastic_DeviceMetrics *deviceMetrics)
{
    meshtastic_NodeInfo info = meshtastic_NodeInfo_init_default;

    info.num = lite->num;
    info.snr = lite->snr;
    info.last_heard = lite->last_heard;
    info.channel = lite->channel;
    info.via_mqtt = nodeInfoLiteViaMqtt(lite);
    info.is_favorite = nodeInfoLiteIsFavorite(lite);
    info.is_ignored = nodeInfoLiteIsIgnored(lite);
    info.is_key_manually_verified = nodeInfoLiteIsKeyManuallyVerified(lite);
    info.is_muted = nodeInfoLiteIsMuted(lite);
    info.has_xeddsa_signed = nodeInfoLiteHasXeddsaSigned(lite);

    if (lite->has_hops_away) {
        info.has_hops_away = true;
        info.hops_away = lite->hops_away;
    }

    if (position) {
        info.has_position = true;
        if (position->latitude_i != 0)
            info.position.has_latitude_i = true;
        info.position.latitude_i = position->latitude_i;
        if (position->longitude_i != 0)
            info.position.has_longitude_i = true;
        info.position.longitude_i = position->longitude_i;
        if (position->altitude != 0)
            info.position.has_altitude = true;
        info.position.altitude = position->altitude;
        info.position.location_source = position->location_source;
        info.position.time = position->time;
        info.position.precision_bits = position->precision_bits;
    }
    if (nodeInfoLiteHasUser(lite)) {
        info.has_user = true;
        info.user = ConvertToUser(lite);
    }
    if (deviceMetrics) {
        info.has_device_metrics = true;
        info.device_metrics = *deviceMetrics;
    }
    return info;
}

meshtastic_NodeInfo TypeConversions::ConvertToNodeInfo(const meshtastic_NodeInfoLite *lite)
{
    meshtastic_PositionLite posScratch;
    meshtastic_DeviceMetrics dmScratch;
    const meshtastic_PositionLite *pos = (nodeDB && nodeDB->copyNodePosition(lite->num, posScratch)) ? &posScratch : nullptr;
    const meshtastic_DeviceMetrics *dm = (nodeDB && nodeDB->copyNodeTelemetry(lite->num, dmScratch)) ? &dmScratch : nullptr;
    return ConvertToNodeInfo(lite, pos, dm);
}

meshtastic_NodeInfo TypeConversions::ConvertToNodeInfoThin(const meshtastic_NodeInfoLite *lite)
{
    return ConvertToNodeInfo(lite, nullptr, nullptr);
}

meshtastic_PositionLite TypeConversions::ConvertToPositionLite(meshtastic_Position position)
{
    meshtastic_PositionLite lite = meshtastic_PositionLite_init_default;
    lite.latitude_i = position.latitude_i;
    lite.longitude_i = position.longitude_i;
    lite.altitude = position.altitude;
    lite.location_source = position.location_source;
    lite.time = position.time;
    lite.precision_bits = position.precision_bits;

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
    // Preserve the peer's broadcast precision; falls back to 0 for entries cached
    // before the precision_bits field existed in PositionLite (pre-migration data).
    // iOS treats 0 as "unspecified precision" and won't render the pin - so for
    // unset values, declare full precision so the stored lat/lon renders as a point.
    position.precision_bits = lite.precision_bits == 0 ? 32 : lite.precision_bits;

    return position;
}

void TypeConversions::CopyUserToNodeInfoLite(meshtastic_NodeInfoLite *lite, const meshtastic_User &user)
{
    if (!lite)
        return;

    strncpy(lite->long_name, user.long_name, sizeof(lite->long_name));
    lite->long_name[sizeof(lite->long_name) - 1] = '\0';
    sanitizeUtf8(lite->long_name, sizeof(lite->long_name));
    strncpy(lite->short_name, user.short_name, sizeof(lite->short_name));
    lite->short_name[sizeof(lite->short_name) - 1] = '\0';
    sanitizeUtf8(lite->short_name, sizeof(lite->short_name));
    lite->hw_model = user.hw_model;
    lite->role = user.role;
    memcpy(lite->public_key.bytes, user.public_key.bytes, sizeof(lite->public_key.bytes));
    lite->public_key.size = user.public_key.size;

    nodeInfoLiteSetBit(lite, NODEINFO_BITFIELD_IS_LICENSED_MASK, user.is_licensed);
    nodeInfoLiteSetBit(lite, NODEINFO_BITFIELD_HAS_IS_UNMESSAGABLE_MASK, user.has_is_unmessagable);
    nodeInfoLiteSetBit(lite, NODEINFO_BITFIELD_IS_UNMESSAGABLE_MASK, user.has_is_unmessagable && user.is_unmessagable);
    nodeInfoLiteSetBit(lite, NODEINFO_BITFIELD_HAS_USER_MASK, true);
}

meshtastic_User TypeConversions::ConvertToUser(const meshtastic_NodeInfoLite *lite)
{
    meshtastic_User user = meshtastic_User_init_default;
    if (!lite)
        return user;

    snprintf(user.id, sizeof(user.id), "!%08x", lite->num);
    strncpy(user.long_name, lite->long_name, sizeof(user.long_name));
    user.long_name[sizeof(user.long_name) - 1] = '\0';
    sanitizeUtf8(user.long_name, sizeof(user.long_name));
    strncpy(user.short_name, lite->short_name, sizeof(user.short_name));
    user.short_name[sizeof(user.short_name) - 1] = '\0';
    sanitizeUtf8(user.short_name, sizeof(user.short_name));
    user.hw_model = lite->hw_model;
    user.role = lite->role;
    user.is_licensed = nodeInfoLiteIsLicensed(lite);
    memcpy(user.public_key.bytes, lite->public_key.bytes, sizeof(user.public_key.bytes));
    user.public_key.size = lite->public_key.size;
    user.has_is_unmessagable = nodeInfoLiteHasIsUnmessagable(lite);
    user.is_unmessagable = nodeInfoLiteIsUnmessagable(lite);
    // macaddr is gone from the slim header; zero-fill for old clients that read it.
    memset(user.macaddr, 0, sizeof(user.macaddr));

    return user;
}
