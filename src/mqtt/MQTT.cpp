#include "MQTT.h"
#include "NodeDB.h"
#include "main.h"
#include "mesh/Channels.h"
#include "mesh/generated/mqtt.pb.h"
#include <WiFi.h>
#include <assert.h>

MQTT *mqtt;

String statusTopic = "mesh/stat/";
String cryptTopic = "mesh/crypt/"; // mesh/crypt/CHANNELID/NODEID

void MQTT::mqttCallback(char *topic, byte *payload, unsigned int length)
{
    mqtt->onPublish(topic, payload, length);
}

void MQTT::onPublish(char *topic, byte *payload, unsigned int length)
{
    // parsing ServiceEnvelope
    ServiceEnvelope e = ServiceEnvelope_init_default;
    if (!pb_decode_from_bytes(payload, length, ServiceEnvelope_fields, &e)) {
        DEBUG_MSG("Invalid MQTT service envelope, topic %s, len %u!\n", topic, length);
    } else {
        DEBUG_MSG("Received MQTT topic %s, len=%u\n", topic, length);

        // FIXME, ignore messages sent by us (requires decryption) or if we don't have the channel key

        // make sure to free both strings and the MeshPacket (passing in NULL is acceptable)
        free(e.channel_id);
        free(e.gateway_id);
        free(e.packet);
    }
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
        // pubSub.subscribe(subsStr, 1); // we use qos 1 because we don't want to miss messages

        /// FIXME, include more information in the status text
        bool ok = pubSub.publish(myStatus.c_str(), "online", true);
        DEBUG_MSG("published %d\n", ok);

        sendSubscriptions();
    } else
        DEBUG_MSG("Failed to contact MQTT server...\n");
}

void MQTT::sendSubscriptions()
{
    size_t numChan = channels.getNumChannels();
    for (size_t i = 0; i < numChan; i++) {
        auto &ch = channels.getByIndex(i);
        if (ch.settings.uplink_enabled) {
            String topic = cryptTopic + channels.getGlobalId(i) + "/#";
            DEBUG_MSG("Subscribing to %s\n", topic.c_str());
            pubSub.subscribe(topic.c_str(), 1); // FIXME, is QOS 1 right?
        }
    }
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
    auto &ch = channels.getByIndex(chIndex);

    // don't bother sending if not connected...
    if (pubSub.connected() && ch.settings.uplink_enabled) {
        const char *channelId = channels.getGlobalId(chIndex); // FIXME, for now we just use the human name for the channel

        ServiceEnvelope env = ServiceEnvelope_init_default;
        env.channel_id = (char *)channelId;
        env.gateway_id = owner.id;
        env.packet = (MeshPacket *)&mp;

        // FIXME - this size calculation is super sloppy, but it will go away once we dynamically alloc meshpackets
        static uint8_t bytes[MeshPacket_size + 64];
        size_t numBytes = pb_encode_to_bytes(bytes, sizeof(bytes), ServiceEnvelope_fields, &env);

        String topic = cryptTopic + channelId + "/" + owner.id;
        DEBUG_MSG("publish %s, %u bytes\n", topic.c_str(), numBytes);

        pubSub.publish(topic.c_str(), bytes, numBytes, false);
    }
}
