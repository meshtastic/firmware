#include "MQTTPlugin.h"
#include "MQTT.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"

MQTTPlugin::MQTTPlugin() : MeshPlugin("mqtt")
{
    isPromiscuous = true; // We always want to update our nodedb, even if we are sniffing on others
}

bool MQTTPlugin::handleReceived(const MeshPacket &mp)
{
    mqtt->publish(mp);
    return false; // never claim handled
}