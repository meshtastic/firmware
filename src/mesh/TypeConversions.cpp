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

    if (lite->has_position) {
        info.has_position = true;
        info.position.latitude_i = lite->position.latitude_i;
        info.position.longitude_i = lite->position.longitude_i;
        info.position.altitude = lite->position.altitude;
        info.position.location_source = lite->position.location_source;
        info.position.time = lite->position.time;
    }
    if (lite->has_user) {
        info.has_user = true;
        info.user = lite->user;
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
    position.latitude_i = lite.latitude_i;
    position.longitude_i = lite.longitude_i;
    position.altitude = lite.altitude;
    position.location_source = lite.location_source;
    position.time = lite.time;

    return position;
}