#include "MQTT.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "main.h"
#include "mesh/Channels.h"
#include "mesh/Router.h"
#include "mesh/generated/mqtt.pb.h"
#include "sleep.h"
#include <WiFi.h>
#include <assert.h>

MQTT *mqtt;

String statusTopic = "msh/1/stat/";
String cryptTopic = "msh/1/c/"; // msh/1/c/CHANNELID/NODEID

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
        if (strcmp(e.gateway_id, owner.id) == 0)
            DEBUG_MSG("Ignoring downlink message we originally sent.\n");
        else {
            if (e.packet) {
                DEBUG_MSG("Received MQTT topic %s, len=%u\n", topic, length);
                MeshPacket *p = packetPool.allocCopy(*e.packet);

                // ignore messages sent by us or if we don't have the channel key
                if (router && p->from != nodeDB.getNodeNum() && perhapsDecode(p))
                    router->enqueueReceivedMessage(p);
                else
                    packetPool.release(p);
            }
        }

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

    // preflightSleepObserver.observe(&preflightSleep);
}

void MQTT::reconnect()
{
    if (wantsLink()) {
        const char *serverAddr = "mqtt.meshtastic.org"; // default hostname
        int serverPort = 1883;                          // default server port
        const char *mqttUsername = "meshdev";
        const char *mqttPassword = "large4cats";

        if (*radioConfig.preferences.mqtt_server) {
            serverAddr = radioConfig.preferences.mqtt_server; // Override the default
            mqttUsername = radioConfig.preferences.mqtt_username; //do not use the hardcoded credentials for a custom mqtt server
            mqttPassword = radioConfig.preferences.mqtt_password;
        } else {
            //we are using the default server.  Use the hardcoded credentials by default, but allow overriding
            if (*radioConfig.preferences.mqtt_username && radioConfig.preferences.mqtt_username[0] != '\0') {
                mqttUsername = radioConfig.preferences.mqtt_username;
            }
            if (*radioConfig.preferences.mqtt_password && radioConfig.preferences.mqtt_password[0] != '\0') {
                mqttPassword = radioConfig.preferences.mqtt_password;
            }
        }

        String server = String(serverAddr);
        int delimIndex = server.indexOf(':');
        if (delimIndex > 0) {
            String port = server.substring(delimIndex + 1, server.length());
            server[delimIndex] = 0;
            serverPort = port.toInt();
            serverAddr = server.c_str();
        }
        pubSub.setServer(serverAddr, serverPort);

        DEBUG_MSG("Connecting to MQTT server %s, port: %d, username: %s, password: %s\n", serverAddr, serverPort, mqttUsername, mqttPassword);
        auto myStatus = (statusTopic + owner.id);
        bool connected = pubSub.connect(owner.id, mqttUsername, mqttPassword, myStatus.c_str(), 1, true, "offline");
        if (connected) {
            DEBUG_MSG("MQTT connected\n");
            enabled = true; // Start running background process again
            runASAP = true;

            /// FIXME, include more information in the status text
            bool ok = pubSub.publish(myStatus.c_str(), "online", true);
            DEBUG_MSG("published %d\n", ok);

            sendSubscriptions();
        } else
            DEBUG_MSG("Failed to contact MQTT server...\n");
    }
}

void MQTT::sendSubscriptions()
{
    size_t numChan = channels.getNumChannels();
    for (size_t i = 0; i < numChan; i++) {
        auto &ch = channels.getByIndex(i);
        if (ch.settings.downlink_enabled) {
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

        powerFSM.trigger(EVENT_CONTACT_FROM_PHONE); // Suppress entering light sleep (because that would turn off bluetooth)
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
