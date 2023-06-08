#include "mesh/generated/meshtastic/mesh.pb.h"

meshtastic_NodeInfoLite ConvertToNodeInfoLite(meshtastic_NodeInfo *node)
{
    meshtastic_NodeInfoLite lite = meshtastic_NodeInfoLite_init_default;

    if (node == NULL)
    {
        return NULL;
    }
    lite.num = node->num;
    lite.snr = node->snr;
    lite.last_heard = node->last_heard;
    lite.channel = node->channel;

    if (node->has_position)
    {
        lite.position = ConvertToPositionLite(node->position);
    }
    if (node->has_position)
    {
        lite.position = ConvertToPositionLite(node->position);
    }
    if (node->has_user)
    {
        lite.user = node->user;
    }
    if (node->has_device_metrics)
    {
        lite.device_metrics = node->device_metrics;
    }
    return info;
}

meshtastic_PositionLite ConvertToPositionLite(meshtastic_Position position)
{
    meshtastic_PositionLite lite = meshtastic_NodeInfoLite_init_default;
    lite.latitude_i = position.latitude_i;
    lite.longitude_i = position.longitude_i;
    lite.altitude = position.altitude;
    lite.location_source = position.location_source;
    lite.time = position.time;

    return lite;
}