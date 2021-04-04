#include "MQTT.h"
#include "NodeDB.h"
#include "main.h"
#include "mesh/Channels.h"
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
    new MQTT();
}

MQTT::MQTT() : concurrency::OSThread("mqtt"), pubSub(mqttClient)
{
    assert(!mqtt);
    mqtt = this;

    pubSub.setCallback(mqttCallback);
}

void MQTT::reconnect()
{
    // pubSub.setServer("devsrv.ezdevice.net", 1883); or 192.168.10.188
    const char *serverAddr = "test.mosquitto.org"; // "mqtt.meshtastic.org"; // default hostname

    if (*radioConfig.preferences.mqtt_server)
        serverAddr = radioConfig.preferences.mqtt_server; // Override the default

    pubSub.setServer(serverAddr, 1883);

    DEBUG_MSG("Connecting to MQTT server\n", serverAddr);
    auto myStatus = (statusTopic + owner.id);
    // bool connected = pubSub.connect(nodeId.c_str(), "meshdev", "apes4cats", myStatus.c_str(), 1, true, "offline");
    bool connected = pubSub.connect(owner.id, myStatus.c_str(), 1, true, "offline");
    if (connected) {
        DEBUG_MSG("MQTT connected\n");
        enabled = true; // Start running background process again
        runASAP = true;

        static char subsStr[64]; /* We keep this static because the mqtt lib
                                    might not be copying it */
        // snprintf(subsStr, sizeof(subsStr), "/ezd/todev/%s/#", clientId);
        // mqtt.subscribe(subsStr, 1); // we use qos 1 because we don't want to miss messages

        /// FIXME, include more information in the status text
        bool ok = pubSub.publish(myStatus.c_str(), "online", true);
        DEBUG_MSG("published %d\n", ok);
    } else
        DEBUG_MSG("Failed to contact MQTT server...\n");
}

bool MQTT::wantsLink() const
{
    bool hasChannel = false;

    if (radioConfig.preferences.mqtt_disabled) {
        // DEBUG_MSG("MQTT disabled...\n");
    } else {
        // No need for link if no channel needed it
        size_t numChan = channels.getNumChannels();
        for (size_t i = 0; i < numChan; i++) {
            auto &ch = channels.getByIndex(i);
            if (ch.settings.uplink_enabled || ch.settings.downlink_enabled) {
                hasChannel = true;
                break;
            }
        }
    }

    return hasChannel && WiFi.isConnected();
}

int32_t MQTT::runOnce()
{
    bool wantConnection = wantsLink();

    // If connected poll rapidly, otherwise only occasionally check for a wifi connection change and ability to contact server
    if (!pubSub.loop()) {
        if (wantConnection) {
            reconnect();

            // If we succeeded, start reading rapidly, else try again in 30 seconds (TCP connections are EXPENSIVE so try rarely)
            return pubSub.connected() ? 20 : 30000;
        } else
            return 5000; // If we don't want connection now, check again in 5 secs
    } else {
        // we are connected to server, check often for new requests on the TCP port
        if (!wantConnection) {
            DEBUG_MSG("MQTT link not needed, dropping\n");
            pubSub.disconnect();
        }

        return 20;
    }
}

void MQTT::onSend(const MeshPacket &mp, ChannelIndex chIndex)
{
    // don't bother sending if not connected...
    if (pubSub.connected()) {
        // FIXME - check uplink enabled

        const char *channelId = channels.getName(chIndex); // FIXME, for now we just use the human name for the channel

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