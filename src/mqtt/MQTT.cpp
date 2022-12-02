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
#if HAS_WIFI
#include <WiFi.h>
#endif
#include <assert.h>
#include "mqtt/JSON.h"

MQTT *mqtt;

String statusTopic = "msh/2/stat/";
String cryptTopic = "msh/2/c/";   // msh/2/c/CHANNELID/NODEID
String jsonTopic = "msh/2/json/"; // msh/2/json/CHANNELID/NODEID

void MQTT::mqttCallback(char *topic, byte *payload, unsigned int length)
{
    mqtt->onPublish(topic, payload, length);
}

void MQTT::onPublish(char *topic, byte *payload, unsigned int length)
{
    // parsing ServiceEnvelope
    ServiceEnvelope e = ServiceEnvelope_init_default;

    if (moduleConfig.mqtt.json_enabled && (strncmp(topic, jsonTopic.c_str(), jsonTopic.length()) == 0)) {
        // check if this is a json payload message by comparing the topic start
        char payloadStr[length + 1];
        memcpy(payloadStr, payload, length);
        payloadStr[length] = 0; // null terminated string
        JSONValue *json_value = JSON::Parse(payloadStr);
        if (json_value != NULL) {
            DEBUG_MSG("JSON Received on MQTT, parsing..\n");
            // check if it is a valid envelope
            JSONObject json;
            json = json_value->AsObject();
            if ((json.find("sender") != json.end()) && (json.find("payload") != json.end()) && (json.find("type") != json.end()) && json["type"]->IsString() && (json["type"]->AsString().compare("sendtext") == 0)) {
                // this is a valid envelope
                if (json["payload"]->IsString() && json["type"]->IsString() && (json["sender"]->AsString().compare(owner.id) != 0)) {
                    std::string jsonPayloadStr = json["payload"]->AsString();
                    DEBUG_MSG("JSON payload %s, length %u\n", jsonPayloadStr.c_str(), jsonPayloadStr.length());

                    // construct protobuf data packet using TEXT_MESSAGE, send it to the mesh
                    MeshPacket *p = router->allocForSending();
                    p->decoded.portnum = PortNum_TEXT_MESSAGE_APP;
                    if (jsonPayloadStr.length() <= sizeof(p->decoded.payload.bytes)) {
                        memcpy(p->decoded.payload.bytes, jsonPayloadStr.c_str(), jsonPayloadStr.length());
                        p->decoded.payload.size = jsonPayloadStr.length();
                        MeshPacket *packet = packetPool.allocCopy(*p);
                        service.sendToMesh(packet, RX_SRC_LOCAL);
                    } else {
                        DEBUG_MSG("Received MQTT json payload too long, dropping\n");
                    }
                } else {
                    DEBUG_MSG("JSON Ignoring downlink message we originally sent.\n");
                }
            } else {
                DEBUG_MSG("JSON Received payload on MQTT but not a valid envelope\n");
            }
        } else {
            // no json, this is an invalid payload
            DEBUG_MSG("Invalid MQTT service envelope, topic %s, len %u!\n", topic, length);
        }
        delete json_value;
    } else {
        if (!pb_decode_from_bytes(payload, length, ServiceEnvelope_fields, &e)) {
            DEBUG_MSG("Invalid MQTT service envelope, topic %s, len %u!\n", topic, length);
            return;
        }else {
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

bool MQTT::connected()
{
    return pubSub.connected();
}

void MQTT::reconnect()
{
    if (wantsLink()) {
        // Defaults
        int serverPort = 1883;  
        const char *serverAddr = default_mqtt_address;
        const char *mqttUsername = default_mqtt_username;
        const char *mqttPassword = default_mqtt_password;

        if (*moduleConfig.mqtt.address) {
            serverAddr = moduleConfig.mqtt.address;
            mqttUsername = moduleConfig.mqtt.username;
            mqttPassword = moduleConfig.mqtt.password;
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
            if (moduleConfig.mqtt.json_enabled == true) {
                String topicDecoded = jsonTopic + channels.getGlobalId(i) + "/#";
                DEBUG_MSG("Subscribing to %s\n", topicDecoded.c_str());
                pubSub.subscribe(topicDecoded.c_str(), 1); // FIXME, is QOS 1 right?
            }
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

#if HAS_WIFI
    return hasChannel && WiFi.isConnected();
#endif
#if HAS_ETHERNET
    return hasChannel && (Ethernet.linkStatus() == LinkON);
#endif
    return false;
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
            auto jsonString = this->downstreamPacketToJson((MeshPacket *)&mp);
            if (jsonString.length() != 0) {
                String topicJson = jsonTopic + channelId + "/" + owner.id;
                DEBUG_MSG("JSON publish message to %s, %u bytes: %s\n", topicJson.c_str(), jsonString.length(), jsonString.c_str());
                pubSub.publish(topicJson.c_str(), jsonString.c_str(), false);
            }
        }
    }
}

// converts a downstream packet into a json message
std::string MQTT::downstreamPacketToJson(MeshPacket *mp)
{
    // the created jsonObj is immutable after creation, so
    // we need to do the heavy lifting before assembling it.
    String msgType;
    JSONObject msgPayload;
    JSONObject jsonObj;

    switch (mp->decoded.portnum) {
    case PortNum_TEXT_MESSAGE_APP: {
        msgType = "text";
        // convert bytes to string
        DEBUG_MSG("got text message of size %u\n", mp->decoded.payload.size);
        char payloadStr[(mp->decoded.payload.size) + 1];
        memcpy(payloadStr, mp->decoded.payload.bytes, mp->decoded.payload.size);
        payloadStr[mp->decoded.payload.size] = 0; // null terminated string
        // check if this is a JSON payload
        JSONValue *json_value = JSON::Parse(payloadStr);
        if (json_value != NULL) {
            DEBUG_MSG("text message payload is of type json\n");
            // if it is, then we can just use the json object
            jsonObj["payload"] = json_value;
        } else {
            // if it isn't, then we need to create a json object
            // with the string as the value
            DEBUG_MSG("text message payload is of type plaintext\n");
            msgPayload["text"] = new JSONValue(payloadStr);
            jsonObj["payload"] = new JSONValue(msgPayload);
        }
        break;
    }
    case PortNum_TELEMETRY_APP: {
        msgType = "telemetry";
        Telemetry scratch;
        Telemetry *decoded = NULL;
        if (mp->which_payload_variant == MeshPacket_decoded_tag) {
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &Telemetry_msg, &scratch)) {
                decoded = &scratch;
                if (decoded->which_variant == Telemetry_device_metrics_tag) {
                    msgPayload["battery_level"] = new JSONValue((int)decoded->variant.device_metrics.battery_level);
                    msgPayload["voltage"] = new JSONValue(decoded->variant.device_metrics.voltage);
                    msgPayload["channel_utilization"] = new JSONValue(decoded->variant.device_metrics.channel_utilization);
                    msgPayload["air_util_tx"] = new JSONValue(decoded->variant.device_metrics.air_util_tx);
                } else if (decoded->which_variant == Telemetry_environment_metrics_tag) {
                    msgPayload["temperature"] = new JSONValue(decoded->variant.environment_metrics.temperature);
                    msgPayload["relative_humidity"] = new JSONValue(decoded->variant.environment_metrics.relative_humidity);
                    msgPayload["barometric_pressure"] = new JSONValue(decoded->variant.environment_metrics.barometric_pressure);
                    msgPayload["gas_resistance"] = new JSONValue(decoded->variant.environment_metrics.gas_resistance);
                    msgPayload["voltage"] = new JSONValue(decoded->variant.environment_metrics.voltage);
                    msgPayload["current"] = new JSONValue(decoded->variant.environment_metrics.current);
                }
                jsonObj["payload"] = new JSONValue(msgPayload);
            } else
                DEBUG_MSG("Error decoding protobuf for telemetry message!\n");
        };
        break;
    }
    case PortNum_NODEINFO_APP: {
        msgType = "nodeinfo";
        User scratch;
        User *decoded = NULL;
        if (mp->which_payload_variant == MeshPacket_decoded_tag) {
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &User_msg, &scratch)) {
                decoded = &scratch;
                msgPayload["id"] = new JSONValue(decoded->id);
                msgPayload["longname"] = new JSONValue(decoded->long_name);
                msgPayload["shortname"] = new JSONValue(decoded->short_name);
                msgPayload["hardware"] = new JSONValue(decoded->hw_model);
                jsonObj["payload"] = new JSONValue(msgPayload);
            } else
                DEBUG_MSG("Error decoding protobuf for nodeinfo message!\n");
        };
        break;
    }
    case PortNum_POSITION_APP: {
        msgType = "position";
        Position scratch;
        Position *decoded = NULL;
        if (mp->which_payload_variant == MeshPacket_decoded_tag) {
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &Position_msg, &scratch)) {
                decoded = &scratch;
                if((int)decoded->time){msgPayload["time"] = new JSONValue((int)decoded->time);}
                if ((int)decoded->timestamp){msgPayload["timestamp"] = new JSONValue((int)decoded->timestamp);}
                msgPayload["latitude_i"] = new JSONValue((int)decoded->latitude_i);
                msgPayload["longitude_i"] = new JSONValue((int)decoded->longitude_i);
                if((int)decoded->altitude){msgPayload["altitude"] = new JSONValue((int)decoded->altitude);}
                jsonObj["payload"] = new JSONValue(msgPayload);
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
        if (mp->which_payload_variant == MeshPacket_decoded_tag) {
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &Waypoint_msg, &scratch)) {
                decoded = &scratch;
                msgPayload["id"] = new JSONValue((int)decoded->id);
                msgPayload["name"] = new JSONValue(decoded->name);
                msgPayload["description"] = new JSONValue(decoded->description);
                msgPayload["expire"] = new JSONValue((int)decoded->expire);
                msgPayload["locked"] = new JSONValue(decoded->locked);
                msgPayload["latitude_i"] = new JSONValue((int)decoded->latitude_i);
                msgPayload["longitude_i"] = new JSONValue((int)decoded->longitude_i);
                jsonObj["payload"] = new JSONValue(msgPayload);
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

    jsonObj["id"] = new JSONValue((int)mp->id);
    jsonObj["timestamp"] = new JSONValue((int)mp->rx_time);
    jsonObj["to"] = new JSONValue((int)mp->to);
    jsonObj["from"] = new JSONValue((int)mp->from);
    jsonObj["channel"] = new JSONValue((int)mp->channel);
    jsonObj["type"] = new JSONValue(msgType.c_str());
    jsonObj["sender"] = new JSONValue(owner.id);

    // serialize and write it to the stream
    JSONValue *value = new JSONValue(jsonObj);
    std::string jsonStr = value->Stringify();

    DEBUG_MSG("serialized json message: %s\n", jsonStr.c_str());

    delete value;
    return jsonStr;
}
