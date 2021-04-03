#include "MQTT.h"
#include "MQTTPlugin.h"
#include "NodeDB.h"
#include "mesh/generated/mqtt.pb.h"
#include <WiFi.h>
#include <assert.h>

MQTT *mqtt;

String statusTopic = "mesh/stat/";

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    DEBUG_MSG("MQTT topic %s\n", topic);

    // After parsing ServiceEnvelope
    // FIXME - make sure to free both strings and the MeshPacket
}

void mqttInit()
{
    // FIXME, for now we require the user to specifically set a MQTT server (till tested)
    if (radioConfig.preferences.mqtt_disabled || !*radioConfig.preferences.mqtt_server)
        DEBUG_MSG("MQTT disabled...\n");
    else if (!WiFi.isConnected())
        DEBUG_MSG("WiFi is not connected, can not start MQTT\n");
    else {
        new MQTT();
        new MQTTPlugin();
    }
}

MQTT::MQTT() : pubSub(mqttClient)
{
    assert(!mqtt);
    mqtt = this;

    // pubSub.setServer("devsrv.ezdevice.net", 1883); or 192.168.10.188
    const char *serverAddr = "test.mosquitto.org"; // "mqtt.meshtastic.org"; // default hostname

    if (*radioConfig.preferences.mqtt_server)
        serverAddr = radioConfig.preferences.mqtt_server; // Override the default

    pubSub.setServer(serverAddr, 1883);
    pubSub.setCallback(mqttCallback);

    DEBUG_MSG("Connecting to MQTT server: %s\n", serverAddr);
    auto myStatus = (statusTopic + owner.id);
    // bool connected = pubSub.connect(nodeId.c_str(), "meshdev", "apes4cats", myStatus.c_str(), 1, true, "offline");
    bool connected = pubSub.connect(owner.id, myStatus.c_str(), 1, true, "offline");
    if (connected) {
        DEBUG_MSG("MQTT connected\n");

        static char subsStr[64]; /* We keep this static because the mqtt lib
                                    might not be copying it */
        // snprintf(subsStr, sizeof(subsStr), "/ezd/todev/%s/#", clientId);
        // mqtt.subscribe(subsStr, 1); // we use qos 1 because we don't want to miss messages

        /// FIXME, include more information in the status text
        bool ok = pubSub.publish(myStatus.c_str(), "online", true);
        DEBUG_MSG("published %d\n", ok);
    }
}

void MQTT::publish(const MeshPacket &mp)
{
    // don't bother sending if not connected...
    if (pubSub.connected()) {
        // FIXME - check uplink enabled

        const char *channelId = "fixmechan";

        ServiceEnvelope env = ServiceEnvelope_init_default;
        env.channel_id = (char *)channelId;
        env.gateway_id = owner.id;
        env.packet = (MeshPacket *)&mp;

        // FIXME - this size calculation is super sloppy, but it will go away once we dynamically alloc meshpackets
        static uint8_t bytes[MeshPacket_size + 64];
        size_t numBytes = pb_encode_to_bytes(bytes, sizeof(bytes), ServiceEnvelope_fields, &env);

        const char *topic = getCryptTopic(channelId);
        DEBUG_MSG("publish %s, %u bytes\n", topic, numBytes);

        pubSub.publish(topic, bytes, numBytes, false);
    }
}

const char *MQTT::getCryptTopic(const char *channelId)
{
    static char buf[128];

    // "mesh/crypt/CHANNELID/NODEID/PORTID"
    snprintf(buf, sizeof(buf), "mesh/crypt/%s/%s", channelId, owner.id);
    return buf;
}