#include "MQTT.h"
#include "NodeDB.h"
#include <WiFi.h>
#include <assert.h>

MQTT *mqtt;

String statusTopic = "mstat/";
String packetTopic = "mesh/";

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
    else
        new MQTT();
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

void MQTT::publish(const MeshPacket *mp)
{
    // DEBUG_MSG("publish %s = %s\n", suffix.c_str(), payload.c_str());

    // pubSub.publish(getTopic(suffix), payload.c_str(), retained);
}

const char *MQTT::getTopic(String suffix, const char *direction)
{
    static char buf[128];

    // "mesh/crypt/CHANNELID/NODEID/PORTID"
    snprintf(buf, sizeof(buf), "mesh/%s/%s/%s", direction, owner.id, suffix.c_str());
    return buf;
}