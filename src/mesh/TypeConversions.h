#include "mesh/generated/meshtastic/deviceonly.pb.h"
#include "mesh/generated/meshtastic/mesh.pb.h"

inline static const meshtastic_NodeInfoLite *ConvertToNodeInfoLite(const meshtastic_NodeInfo *node)
{
    meshtastic_NodeInfoLite *lite = NULL;
    memset(lite, 0, sizeof(*lite));

    lite->num = node->num;
    lite->snr = node->snr;
    lite->last_heard = node->last_heard;
    lite->channel = node->channel;

    if (node->has_position) {
        lite->position.latitude_i = node->position.latitude_i;
        lite->position.longitude_i = node->position.longitude_i;
        lite->position.altitude = node->position.altitude;
        lite->position.location_source = node->position.location_source;
        lite->position.time = node->position.time;
    }
    if (node->has_user) {
        lite->user = node->user;
    }
    if (node->has_device_metrics) {
        lite->device_metrics = node->device_metrics;
    }
    return lite;
}

inline static const meshtastic_NodeInfo *ConvertToNodeInfo(const meshtastic_NodeInfoLite *lite)
{
    meshtastic_NodeInfo *info = NULL;
    memset(info, 0, sizeof(*info));

    info->num = lite->num;
    info->snr = lite->snr;
    info->last_heard = lite->last_heard;
    info->channel = lite->channel;

    if (lite->has_position) {
        info->position.latitude_i = lite->position.latitude_i;
        info->position.longitude_i = lite->position.longitude_i;
        info->position.altitude = lite->position.altitude;
        info->position.location_source = lite->position.location_source;
        info->position.time = lite->position.time;
    }
    if (lite->has_user) {
        info->user = lite->user;
    }
    if (lite->has_device_metrics) {
        info->device_metrics = lite->device_metrics;
    }
    return info;
}

inline static meshtastic_PositionLite ConvertToPositionLite(meshtastic_Position position)
{
    meshtastic_PositionLite lite = meshtastic_PositionLite_init_default;
    lite.latitude_i = position.latitude_i;
    lite.longitude_i = position.longitude_i;
    lite.altitude = position.altitude;
    lite.location_source = position.location_source;
    lite.time = position.time;

    return lite;
}

inline static meshtastic_Position ConvertToPosition(meshtastic_PositionLite lite)
{
    meshtastic_Position position = meshtastic_Position_init_default;
    position.latitude_i = lite.latitude_i;
    position.longitude_i = lite.longitude_i;
    position.altitude = lite.altitude;
    position.location_source = lite.location_source;
    position.time = lite.time;

    return position;
}