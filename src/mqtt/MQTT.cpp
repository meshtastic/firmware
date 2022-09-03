#include "MQTT.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "main.h"
#include "mesh/Channels.h"
#include "mesh/Router.h"
#include "mesh/generated/mqtt.pb.h"
#include "mesh/generated/telemetry.pb.h"
#include "sleep.h"
#include <WiFi.h>
#include <assert.h>
#include <json11.hpp>

MQTT *mqtt;

String statusTopic = "msh/1/stat/";
String cryptTopic = "msh/1/c/";   // msh/1/c/CHANNELID/NODEID
String jsonTopic = "msh/1/json/"; // msh/1/json/CHANNELID/NODEID

void MQTT::mqttCallback(char *topic, byte *payload, unsigned int length)
{
    mqtt->onPublish(topic, payload, length);
}

void MQTT::onPublish(char *topic, byte *payload, unsigned int length)
{
    // parsing ServiceEnvelope
    ServiceEnvelope e = ServiceEnvelope_init_default;
    if (moduleConfig.mqtt.json_enabled && !pb_decode_from_bytes(payload, length, ServiceEnvelope_fields, &e)) {

        // check if this is a json payload message
        using namespace json11;
        char payloadStr[length + 1];
        memcpy(payloadStr, payload, length);
        payloadStr[length] = 0; // null terminated string
        std::string err;
        auto json = Json::parse(payloadStr, err);
        if (err.empty()) {
            DEBUG_MSG("Received json payload on MQTT, parsing..\n");
            // check if it is a valid envelope
            if (json.object_items().count("sender") != 0 && json.object_items().count("payload") != 0) {
                // this is a valid envelope
                if (json["sender"].string_value().compare(owner.id) != 0) {
                    std::string jsonPayloadStr = json["payload"].dump();
                    DEBUG_MSG("Received json payload %s, length %u\n", jsonPayloadStr.c_str(), jsonPayloadStr.length());
                    // FIXME Not sure we need to be doing this
                    // construct protobuf data packet using TEXT_MESSAGE, send it to the mesh
                    // MeshPacket *p = router->allocForSending();
                    // p->decoded.portnum = PortNum_TEXT_MESSAGE_APP;
                    // if (jsonPayloadStr.length() <= sizeof(p->decoded.payload.bytes)) {
                    //     memcpy(p->decoded.payload.bytes, jsonPayloadStr.c_str(), jsonPayloadStr.length());
                    //     p->decoded.payload.size = jsonPayloadStr.length();
                    //     MeshPacket *packet = packetPool.allocCopy(*p);
                    //     service.sendToMesh(packet, RX_SRC_LOCAL);
                    // } else {
                    //     DEBUG_MSG("Received MQTT json payload too long, dropping\n");
                    // }
                } else {
                    DEBUG_MSG("Ignoring downlink message we originally sent.\n");
                }
            } else {
                DEBUG_MSG("Received json payload on MQTT but not a valid envelope\n");
            }
        } else {
            // no json, this is an invalid payload
            DEBUG_MSG("Invalid MQTT service envelope, topic %s, len %u!\n", topic, length);
        }
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

        if (*moduleConfig.mqtt.address) {
            serverAddr = moduleConfig.mqtt.address; // Override the default
            mqttUsername =
                moduleConfig.mqtt.username; // do not use the hardcoded credentials for a custom mqtt server
            mqttPassword = moduleConfig.mqtt.password;
        } else {
            // we are using the default server.  Use the hardcoded credentials by default, but allow overriding
            if (*moduleConfig.mqtt.username && moduleConfig.mqtt.username[0] != '\0') {
                mqttUsername = moduleConfig.mqtt.username;
            }
            if (*moduleConfig.mqtt.password && moduleConfig.mqtt.password[0] != '\0') {
                mqttPassword = moduleConfig.mqtt.password;
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

        DEBUG_MSG("Connecting to MQTT server %s, port: %d, username: %s, password: %s\n", serverAddr, serverPort, mqttUsername,
                  mqttPassword);
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
            String topicDecoded = jsonTopic + channels.getGlobalId(i) + "/#";
            DEBUG_MSG("Subscribing to %s\n", topicDecoded.c_str());
            pubSub.subscribe(topicDecoded.c_str(), 1); // FIXME, is QOS 1 right?
        }
    }
}

bool MQTT::wantsLink() const
{
    bool hasChannel = false;

    if (moduleConfig.mqtt.enabled) {
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

        if (moduleConfig.mqtt.json_enabled) {
            // handle json topic
            using namespace json11;
            auto jsonString = this->downstreamPacketToJson((MeshPacket *)&mp);
            if (jsonString.length() != 0) {
                String topicJson = jsonTopic + channelId + "/" + owner.id;
                DEBUG_MSG("publish json message to %s, %u bytes: %s\n", topicJson.c_str(), jsonString.length(), jsonString.c_str());
                pubSub.publish(topicJson.c_str(), jsonString.c_str(), false);
            }
        }
    }
}

// converts a downstream packet into a json message
String MQTT::downstreamPacketToJson(MeshPacket *mp)
{
    using namespace json11;

    // the created jsonObj is immutable after creation, so
    // we need to do the heavy lifting before assembling it.
    String msgType;
    Json msgPayload;

    switch (mp->decoded.portnum) {
    case PortNum_TEXT_MESSAGE_APP: {
        msgType = "text";
        // convert bytes to string
        DEBUG_MSG("got text message of size %u\n", mp->decoded.payload.size);
        char payloadStr[(mp->decoded.payload.size) + 1];
        memcpy(payloadStr, mp->decoded.payload.bytes, mp->decoded.payload.size);
        payloadStr[mp->decoded.payload.size] = 0; // null terminated string
        // check if this is a JSON payload
        std::string err;
        auto json = Json::parse(payloadStr, err);
        if (err.empty()) {
            DEBUG_MSG("text message payload is of type json\n");
            // if it is, then we can just use the json object
            msgPayload = json;
        } else {
            // if it isn't, then we need to create a json object
            // with the string as the value
            DEBUG_MSG("text message payload is of type plaintext\n");
            msgPayload = Json::object({{"text", payloadStr}});
        }
        break;
    }
    case PortNum_TELEMETRY_APP: {
        msgType = "telemetry";
        Telemetry scratch;
        Telemetry *decoded = NULL;
        if (mp->which_payloadVariant == MeshPacket_decoded_tag) {
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &Telemetry_msg, &scratch)) {
                decoded = &scratch;
                if (decoded->which_variant == Telemetry_device_metrics_tag) {
                    msgPayload = Json::object{
                        {"battery_level", (int)decoded->variant.device_metrics.battery_level},
                        {"voltage", decoded->variant.device_metrics.voltage},
                        {"channel_utilization", decoded->variant.device_metrics.channel_utilization},
                        {"air_util_tx", decoded->variant.device_metrics.air_util_tx},
                    };
                } else if (decoded->which_variant == Telemetry_environment_metrics_tag) {
                    msgPayload = Json::object{
                        {"temperature", decoded->variant.environment_metrics.temperature},
                        {"relative_humidity", decoded->variant.environment_metrics.relative_humidity},
                        {"barometric_pressure", decoded->variant.environment_metrics.barometric_pressure},
                        {"gas_resistance", decoded->variant.environment_metrics.gas_resistance},
                        {"voltage", decoded->variant.environment_metrics.voltage},
                        {"current", decoded->variant.environment_metrics.current},
                    };
                }
            } else
                DEBUG_MSG("Error decoding protobuf for telemetry message!\n");
        };
        break;
    }
    case PortNum_NODEINFO_APP: {
        msgType = "nodeinfo";
        User scratch;
        User *decoded = NULL;
        if (mp->which_payloadVariant == MeshPacket_decoded_tag) {
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &User_msg, &scratch)) {
                decoded = &scratch;
                msgPayload = Json::object{
                    {"id", decoded->id},
                    {"longname", decoded->long_name},
                    {"shortname", decoded->short_name},
                    {"hardware", decoded->hw_model}
                };

            } else
                DEBUG_MSG("Error decoding protobuf for nodeinfo message!\n");
        };
        break;
    }
    case PortNum_POSITION_APP: {
        msgType = "position";
        Position scratch;
        Position *decoded = NULL;
        if (mp->which_payloadVariant == MeshPacket_decoded_tag) {
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &Position_msg, &scratch)) {
                decoded = &scratch;
                msgPayload = Json::object{
                    {"time", (int)decoded->time},
                    {"pos_timestamp", (int)decoded->pos_timestamp},
                    {"latitude_i", decoded->latitude_i}, 
                    {"longitude_i", decoded->longitude_i}, 
                    {"altitude", decoded->altitude}
                };
            } else {
                DEBUG_MSG("Error decoding protobuf for position message!\n");
            }
        };
        break;
    }

    case PortNum_WAYPOINT_APP: {
        msgType = "position";
        Waypoint scratch;
        Waypoint *decoded = NULL;
        if (mp->which_payloadVariant == MeshPacket_decoded_tag) {
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &Waypoint_msg, &scratch)) {
                decoded = &scratch;
                msgPayload = Json::object{
                    {"id", (int)decoded->id},
                    {"name", decoded->name},
                    {"description", decoded->description},
                    {"expire", (int)decoded->expire},
                    {"locked", decoded->locked},
                    {"latitude_i", decoded->latitude_i}, 
                    {"longitude_i", decoded->longitude_i}, 
                };
            } else {
                DEBUG_MSG("Error decoding protobuf for position message!\n");
            }
        };
        break;
    }
    // add more packet types here if needed
    default:
        break;
    }

    // assemble the final jsonObj
    Json jsonObj = Json::object{
        {"id", Json((int)mp->id)},
        {"timestamp", Json((int)mp->rx_time)},
        {"to", Json((int)mp->to)},
        {"from", Json((int)mp->from)},
        {"channel", Json((int)mp->channel)},
        {"type", msgType.c_str()},
        {"sender", owner.id},
        {"payload", msgPayload}
    };

    // serialize and return it
    static std::string jsonStr = jsonObj.dump();
    DEBUG_MSG("serialized json message: %s\n", jsonStr.c_str());

    return jsonStr.c_str();
}
