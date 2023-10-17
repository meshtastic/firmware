#include "MQTT.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "main.h"
#include "mesh/Channels.h"
#include "mesh/Router.h"
#include "mesh/generated/meshtastic/mqtt.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include "mesh/http/WiFiAPClient.h"
#include "sleep.h"
#if HAS_WIFI
#include <WiFi.h>
#endif
#include "mqtt/JSON.h"
#include <assert.h>

const int reconnectMax = 5;

MQTT *mqtt;

static MemoryDynamic<meshtastic_ServiceEnvelope> staticMqttPool;

Allocator<meshtastic_ServiceEnvelope> &mqttPool = staticMqttPool;

void MQTT::mqttCallback(char *topic, byte *payload, unsigned int length)
{
    mqtt->onReceive(topic, payload, length);
}

void MQTT::onClientProxyReceive(meshtastic_MqttClientProxyMessage msg)
{
    onReceive(msg.topic, msg.payload_variant.data.bytes, msg.payload_variant.data.size);
}

void MQTT::onReceive(char *topic, byte *payload, size_t length)
{
    meshtastic_ServiceEnvelope e = meshtastic_ServiceEnvelope_init_default;

    if (moduleConfig.mqtt.json_enabled && (strncmp(topic, jsonTopic.c_str(), jsonTopic.length()) == 0)) {
        // check if this is a json payload message by comparing the topic start
        char payloadStr[length + 1];
        memcpy(payloadStr, payload, length);
        payloadStr[length] = 0; // null terminated string
        JSONValue *json_value = JSON::Parse(payloadStr);
        if (json_value != NULL) {
            LOG_INFO("JSON Received on MQTT, parsing..\n");

            // check if it is a valid envelope
            JSONObject json;
            json = json_value->AsObject();

            // parse the channel name from the topic string
            char *ptr = strtok(topic, "/");
            for (int i = 0; i < 3; i++) {
                ptr = strtok(NULL, "/");
            }
            meshtastic_Channel sendChannel = channels.getByName(ptr);
            LOG_DEBUG("Found Channel name: %s (Index %d)\n", channels.getGlobalId(sendChannel.index), sendChannel.index);

            if ((json.find("sender") != json.end()) && (json.find("payload") != json.end()) &&
                (json.find("type") != json.end()) && json["type"]->IsString() &&
                (json["type"]->AsString().compare("sendtext") == 0)) {
                // this is a valid envelope
                if (json["payload"]->IsString() && json["type"]->IsString() &&
                    (json["sender"]->AsString().compare(owner.id) != 0)) {
                    std::string jsonPayloadStr = json["payload"]->AsString();
                    LOG_INFO("JSON payload %s, length %u\n", jsonPayloadStr.c_str(), jsonPayloadStr.length());

                    // construct protobuf data packet using TEXT_MESSAGE, send it to the mesh
                    meshtastic_MeshPacket *p = router->allocForSending();
                    p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
                    p->channel = sendChannel.index;
                    if (sendChannel.settings.downlink_enabled) {
                        if (jsonPayloadStr.length() <= sizeof(p->decoded.payload.bytes)) {
                            memcpy(p->decoded.payload.bytes, jsonPayloadStr.c_str(), jsonPayloadStr.length());
                            p->decoded.payload.size = jsonPayloadStr.length();
                            meshtastic_MeshPacket *packet = packetPool.allocCopy(*p);
                            service.sendToMesh(packet, RX_SRC_LOCAL);
                        } else {
                            LOG_WARN("Received MQTT json payload too long, dropping\n");
                        }
                    } else {
                        LOG_WARN("Received MQTT json payload on channel %s, but downlink is disabled, dropping\n",
                                 sendChannel.settings.name);
                    }
                } else {
                    LOG_DEBUG("JSON Ignoring downlink message we originally sent.\n");
                }
            } else if ((json.find("sender") != json.end()) && (json.find("payload") != json.end()) &&
                       (json.find("type") != json.end()) && json["type"]->IsString() &&
                       (json["type"]->AsString().compare("sendposition") == 0)) {
                // invent the "sendposition" type for a valid envelope
                if (json["payload"]->IsObject() && json["type"]->IsString() &&
                    (json["sender"]->AsString().compare(owner.id) != 0)) {
                    JSONObject posit;
                    posit = json["payload"]->AsObject(); // get nested JSON Position
                    meshtastic_Position pos = meshtastic_Position_init_default;
                    pos.latitude_i = posit["latitude_i"]->AsNumber();
                    pos.longitude_i = posit["longitude_i"]->AsNumber();
                    pos.altitude = posit["altitude"]->AsNumber();
                    pos.time = posit["time"]->AsNumber();

                    // construct protobuf data packet using POSITION, send it to the mesh
                    meshtastic_MeshPacket *p = router->allocForSending();
                    p->decoded.portnum = meshtastic_PortNum_POSITION_APP;
                    p->channel = sendChannel.index;
                    if (sendChannel.settings.downlink_enabled) {
                        p->decoded.payload.size =
                            pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
                                               &meshtastic_Position_msg, &pos); // make the Data protobuf from position
                        service.sendToMesh(p, RX_SRC_LOCAL);
                    } else {
                        LOG_WARN("Received MQTT json payload on channel %s, but downlink is disabled, dropping\n",
                                 sendChannel.settings.name);
                    }
                } else {
                    LOG_DEBUG("JSON Ignoring downlink message we originally sent.\n");
                }
            } else {
                LOG_ERROR("JSON Received payload on MQTT but not a valid envelope\n");
            }
        } else {
            // no json, this is an invalid payload
            LOG_ERROR("Invalid MQTT service envelope, topic %s, len %u!\n", topic, length);
        }
        delete json_value;
    } else {
        if (!pb_decode_from_bytes(payload, length, &meshtastic_ServiceEnvelope_msg, &e)) {
            LOG_ERROR("Invalid MQTT service envelope, topic %s, len %u!\n", topic, length);
            return;
        } else {
            if (strcmp(e.gateway_id, owner.id) == 0)
                LOG_INFO("Ignoring downlink message we originally sent.\n");
            else {
                // Find channel by channel_id and check downlink_enabled
                meshtastic_Channel ch = channels.getByName(e.channel_id);
                if (strcmp(e.channel_id, channels.getGlobalId(ch.index)) == 0 && e.packet && ch.settings.downlink_enabled) {
                    LOG_INFO("Received MQTT topic %s, len=%u\n", topic, length);
                    meshtastic_MeshPacket *p = packetPool.allocCopy(*e.packet);

                    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
                        p->channel = ch.index;
                    }

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

#ifdef HAS_NETWORKING
MQTT::MQTT() : concurrency::OSThread("mqtt"), pubSub(mqttClient), mqttQueue(MAX_MQTT_QUEUE)
#else
MQTT::MQTT() : concurrency::OSThread("mqtt"), mqttQueue(MAX_MQTT_QUEUE)
#endif
{
    if (moduleConfig.mqtt.enabled) {
        LOG_DEBUG("Initializing MQTT\n");

        assert(!mqtt);
        mqtt = this;

        if (*moduleConfig.mqtt.root) {
            statusTopic = moduleConfig.mqtt.root + statusTopic;
            cryptTopic = moduleConfig.mqtt.root + cryptTopic;
            jsonTopic = moduleConfig.mqtt.root + jsonTopic;
        } else {
            statusTopic = "msh" + statusTopic;
            cryptTopic = "msh" + cryptTopic;
            jsonTopic = "msh" + jsonTopic;
        }

#ifdef HAS_NETWORKING
        if (!moduleConfig.mqtt.proxy_to_client_enabled)
            pubSub.setCallback(mqttCallback);
#endif

        if (moduleConfig.mqtt.proxy_to_client_enabled) {
            LOG_INFO("MQTT configured to use client proxy...\n");
            enabled = true;
            runASAP = true;
            reconnectCount = 0;
            publishStatus();
        }
        // preflightSleepObserver.observe(&preflightSleep);
    } else {
        disable();
    }
}

bool MQTT::isConnectedDirectly()
{
#ifdef HAS_NETWORKING
    return pubSub.connected();
#else
    return false;
#endif
}

bool MQTT::publish(const char *topic, const char *payload, bool retained)
{
    if (moduleConfig.mqtt.proxy_to_client_enabled) {
        meshtastic_MqttClientProxyMessage *msg = mqttClientProxyMessagePool.allocZeroed();
        msg->which_payload_variant = meshtastic_MqttClientProxyMessage_text_tag;
        strcpy(msg->topic, topic);
        strcpy(msg->payload_variant.text, payload);
        msg->retained = retained;
        service.sendMqttMessageToClientProxy(msg);
        return true;
    }
#ifdef HAS_NETWORKING
    else if (isConnectedDirectly()) {
        return pubSub.publish(topic, payload, retained);
    }
#endif
    return false;
}

bool MQTT::publish(const char *topic, const uint8_t *payload, size_t length, bool retained)
{
    if (moduleConfig.mqtt.proxy_to_client_enabled) {
        meshtastic_MqttClientProxyMessage *msg = mqttClientProxyMessagePool.allocZeroed();
        msg->which_payload_variant = meshtastic_MqttClientProxyMessage_data_tag;
        strcpy(msg->topic, topic);
        msg->payload_variant.data.size = length;
        memcpy(msg->payload_variant.data.bytes, payload, length);
        msg->retained = retained;
        service.sendMqttMessageToClientProxy(msg);
        return true;
    }
#ifdef HAS_NETWORKING
    else if (isConnectedDirectly()) {
        return pubSub.publish(topic, payload, length, retained);
    }
#endif
    return false;
}

void MQTT::reconnect()
{
    if (wantsLink()) {
        if (moduleConfig.mqtt.proxy_to_client_enabled) {
            LOG_INFO("MQTT connecting via client proxy instead...\n");
            enabled = true;
            runASAP = true;
            reconnectCount = 0;

            publishStatus();
            return; // Don't try to connect directly to the server
        }
#ifdef HAS_NETWORKING
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
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
        if (moduleConfig.mqtt.tls_enabled) {
            // change default for encrypted to 8883
            try {
                serverPort = 8883;
                wifiSecureClient.setInsecure();

                pubSub.setClient(wifiSecureClient);
                LOG_INFO("Using TLS-encrypted session\n");
            } catch (const std::exception &e) {
                LOG_ERROR("MQTT ERROR: %s\n", e.what());
            }
        } else {
            LOG_INFO("Using non-TLS-encrypted session\n");
            pubSub.setClient(mqttClient);
        }
#elif HAS_NETWORKING
        pubSub.setClient(mqttClient);
#endif

        String server = String(serverAddr);
        int delimIndex = server.indexOf(':');
        if (delimIndex > 0) {
            String port = server.substring(delimIndex + 1, server.length());
            server[delimIndex] = 0;
            serverPort = port.toInt();
            serverAddr = server.c_str();
        }
        pubSub.setServer(serverAddr, serverPort);
        pubSub.setBufferSize(512);

        LOG_INFO("Attempting to connect directly to MQTT server %s, port: %d, username: %s, password: %s\n", serverAddr,
                 serverPort, mqttUsername, mqttPassword);

        auto myStatus = (statusTopic + owner.id);
        bool connected = pubSub.connect(owner.id, mqttUsername, mqttPassword, myStatus.c_str(), 1, true, "offline");
        if (connected) {
            LOG_INFO("MQTT connected\n");
            enabled = true; // Start running background process again
            runASAP = true;
            reconnectCount = 0;

            publishStatus();
            sendSubscriptions();
        } else {
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
            reconnectCount++;
            LOG_ERROR("Failed to contact MQTT server directly (%d/%d)...\n", reconnectCount, reconnectMax);
            if (reconnectCount >= reconnectMax) {
                needReconnect = true;
                wifiReconnect->setIntervalFromNow(0);
                reconnectCount = 0;
            }
#endif
        }
#endif
    }
}

void MQTT::sendSubscriptions()
{
#ifdef HAS_NETWORKING
    size_t numChan = channels.getNumChannels();
    for (size_t i = 0; i < numChan; i++) {
        const auto &ch = channels.getByIndex(i);
        if (ch.settings.downlink_enabled) {
            std::string topic = cryptTopic + channels.getGlobalId(i) + "/#";
            LOG_INFO("Subscribing to %s\n", topic.c_str());
            pubSub.subscribe(topic.c_str(), 1); // FIXME, is QOS 1 right?
            if (moduleConfig.mqtt.json_enabled == true) {
                std::string topicDecoded = jsonTopic + channels.getGlobalId(i) + "/#";
                LOG_INFO("Subscribing to %s\n", topicDecoded.c_str());
                pubSub.subscribe(topicDecoded.c_str(), 1); // FIXME, is QOS 1 right?
            }
        }
    }
#endif
}

bool MQTT::wantsLink() const
{
    bool hasChannel = false;

    if (moduleConfig.mqtt.enabled) {
        // No need for link if no channel needed it
        size_t numChan = channels.getNumChannels();
        for (size_t i = 0; i < numChan; i++) {
            const auto &ch = channels.getByIndex(i);
            if (ch.settings.uplink_enabled || ch.settings.downlink_enabled) {
                hasChannel = true;
                break;
            }
        }
    }
    if (hasChannel && moduleConfig.mqtt.proxy_to_client_enabled)
        return true;

#if HAS_WIFI
    return hasChannel && WiFi.isConnected();
#endif
#if HAS_ETHERNET
    return hasChannel && Ethernet.linkStatus() == LinkON;
#endif
    return false;
}

int32_t MQTT::runOnce()
{
    if (!moduleConfig.mqtt.enabled)
        return disable();

    bool wantConnection = wantsLink();

    // If connected poll rapidly, otherwise only occasionally check for a wifi connection change and ability to contact server
    if (moduleConfig.mqtt.proxy_to_client_enabled) {
        publishQueuedMessages();
        return 200;
    }
#ifdef HAS_NETWORKING
    else if (!pubSub.loop()) {
        if (!wantConnection)
            return 5000; // If we don't want connection now, check again in 5 secs
        else {
            reconnect();
            // If we succeeded, empty the queue one by one and start reading rapidly, else try again in 30 seconds (TCP
            // connections are EXPENSIVE so try rarely)
            if (isConnectedDirectly()) {
                publishQueuedMessages();
                return 200;
            } else
                return 30000;
        }
    } else {
        // we are connected to server, check often for new requests on the TCP port
        if (!wantConnection) {
            LOG_INFO("MQTT link not needed, dropping\n");
            pubSub.disconnect();
        }

        powerFSM.trigger(EVENT_CONTACT_FROM_PHONE); // Suppress entering light sleep (because that would turn off bluetooth)
        return 20;
    }
#endif
    return 30000;
}

/// FIXME, include more information in the status text
void MQTT::publishStatus()
{
    auto myStatus = (statusTopic + owner.id);
    bool ok = publish(myStatus.c_str(), "online", true);
    LOG_INFO("published online=%d\n", ok);
}

void MQTT::publishQueuedMessages()
{
    if (!mqttQueue.isEmpty()) {
        LOG_DEBUG("Publishing enqueued MQTT message\n");
        // FIXME - this size calculation is super sloppy, but it will go away once we dynamically alloc meshpackets
        meshtastic_ServiceEnvelope *env = mqttQueue.dequeuePtr(0);
        static uint8_t bytes[meshtastic_MeshPacket_size + 64];
        size_t numBytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_ServiceEnvelope_msg, env);

        std::string topic = cryptTopic + env->channel_id + "/" + owner.id;
        LOG_INFO("publish %s, %u bytes from queue\n", topic.c_str(), numBytes);

        publish(topic.c_str(), bytes, numBytes, false);

        if (moduleConfig.mqtt.json_enabled) {
            // handle json topic
            auto jsonString = this->meshPacketToJson(env->packet);
            if (jsonString.length() != 0) {
                std::string topicJson = jsonTopic + env->channel_id + "/" + owner.id;
                LOG_INFO("JSON publish message to %s, %u bytes: %s\n", topicJson.c_str(), jsonString.length(),
                         jsonString.c_str());
                publish(topicJson.c_str(), jsonString.c_str(), false);
            }
        }
        mqttPool.release(env);
    }
}

void MQTT::onSend(const meshtastic_MeshPacket &mp, ChannelIndex chIndex)
{
    auto &ch = channels.getByIndex(chIndex);

    if (&mp.decoded && strcmp(moduleConfig.mqtt.address, default_mqtt_address) == 0 &&
        (mp.decoded.portnum == meshtastic_PortNum_RANGE_TEST_APP ||
         mp.decoded.portnum == meshtastic_PortNum_DETECTION_SENSOR_APP)) {
        LOG_DEBUG("MQTT onSend - Ignoring range test or detection sensor message on public mqtt\n");
        return;
    }

    if (ch.settings.uplink_enabled) {
        const char *channelId = channels.getGlobalId(chIndex); // FIXME, for now we just use the human name for the channel

        meshtastic_ServiceEnvelope *env = mqttPool.allocZeroed();
        env->channel_id = (char *)channelId;
        env->gateway_id = owner.id;
        env->packet = (meshtastic_MeshPacket *)&mp;
        LOG_DEBUG("MQTT onSend - Publishing portnum %i message\n", env->packet->decoded.portnum);

        if (moduleConfig.mqtt.proxy_to_client_enabled || this->isConnectedDirectly()) {
            // FIXME - this size calculation is super sloppy, but it will go away once we dynamically alloc meshpackets
            static uint8_t bytes[meshtastic_MeshPacket_size + 64];
            size_t numBytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_ServiceEnvelope_msg, env);

            std::string topic = cryptTopic + channelId + "/" + owner.id;
            LOG_DEBUG("MQTT Publish %s, %u bytes\n", topic.c_str(), numBytes);

            publish(topic.c_str(), bytes, numBytes, false);

            if (moduleConfig.mqtt.json_enabled) {
                // handle json topic
                auto jsonString = this->meshPacketToJson((meshtastic_MeshPacket *)&mp);
                if (jsonString.length() != 0) {
                    std::string topicJson = jsonTopic + channelId + "/" + owner.id;
                    LOG_INFO("JSON publish message to %s, %u bytes: %s\n", topicJson.c_str(), jsonString.length(),
                             jsonString.c_str());
                    publish(topicJson.c_str(), jsonString.c_str(), false);
                }
            }
        } else {
            LOG_INFO("MQTT not connected, queueing packet\n");
            if (mqttQueue.numFree() == 0) {
                LOG_WARN("NOTE: MQTT queue is full, discarding oldest\n");
                meshtastic_ServiceEnvelope *d = mqttQueue.dequeuePtr(0);
                if (d)
                    mqttPool.release(d);
            }
            // make a copy of serviceEnvelope and queue it
            meshtastic_ServiceEnvelope *copied = mqttPool.allocCopy(*env);
            assert(mqttQueue.enqueue(copied, 0));
        }
        mqttPool.release(env);
    }
}

// converts a downstream packet into a json message
std::string MQTT::meshPacketToJson(meshtastic_MeshPacket *mp)
{
    // the created jsonObj is immutable after creation, so
    // we need to do the heavy lifting before assembling it.
    std::string msgType;
    JSONObject msgPayload;
    JSONObject jsonObj;

    if (mp->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        switch (mp->decoded.portnum) {
        case meshtastic_PortNum_TEXT_MESSAGE_APP: {
            msgType = "text";
            // convert bytes to string
            LOG_DEBUG("got text message of size %u\n", mp->decoded.payload.size);
            char payloadStr[(mp->decoded.payload.size) + 1];
            memcpy(payloadStr, mp->decoded.payload.bytes, mp->decoded.payload.size);
            payloadStr[mp->decoded.payload.size] = 0; // null terminated string
            // check if this is a JSON payload
            JSONValue *json_value = JSON::Parse(payloadStr);
            if (json_value != NULL) {
                LOG_INFO("text message payload is of type json\n");
                // if it is, then we can just use the json object
                jsonObj["payload"] = json_value;
            } else {
                // if it isn't, then we need to create a json object
                // with the string as the value
                LOG_INFO("text message payload is of type plaintext\n");
                msgPayload["text"] = new JSONValue(payloadStr);
                jsonObj["payload"] = new JSONValue(msgPayload);
            }
            break;
        }
        case meshtastic_PortNum_TELEMETRY_APP: {
            msgType = "telemetry";
            meshtastic_Telemetry scratch;
            meshtastic_Telemetry *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_Telemetry_msg, &scratch)) {
                decoded = &scratch;
                if (decoded->which_variant == meshtastic_Telemetry_device_metrics_tag) {
                    msgPayload["battery_level"] = new JSONValue((uint)decoded->variant.device_metrics.battery_level);
                    msgPayload["voltage"] = new JSONValue(decoded->variant.device_metrics.voltage);
                    msgPayload["channel_utilization"] = new JSONValue(decoded->variant.device_metrics.channel_utilization);
                    msgPayload["air_util_tx"] = new JSONValue(decoded->variant.device_metrics.air_util_tx);
                } else if (decoded->which_variant == meshtastic_Telemetry_environment_metrics_tag) {
                    msgPayload["temperature"] = new JSONValue(decoded->variant.environment_metrics.temperature);
                    msgPayload["relative_humidity"] = new JSONValue(decoded->variant.environment_metrics.relative_humidity);
                    msgPayload["barometric_pressure"] = new JSONValue(decoded->variant.environment_metrics.barometric_pressure);
                    msgPayload["gas_resistance"] = new JSONValue(decoded->variant.environment_metrics.gas_resistance);
                    msgPayload["voltage"] = new JSONValue(decoded->variant.environment_metrics.voltage);
                    msgPayload["current"] = new JSONValue(decoded->variant.environment_metrics.current);
                }
                jsonObj["payload"] = new JSONValue(msgPayload);
            } else {
                LOG_ERROR("Error decoding protobuf for telemetry message!\n");
            }
            break;
        }
        case meshtastic_PortNum_NODEINFO_APP: {
            msgType = "nodeinfo";
            meshtastic_User scratch;
            meshtastic_User *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_User_msg, &scratch)) {
                decoded = &scratch;
                msgPayload["id"] = new JSONValue(decoded->id);
                msgPayload["longname"] = new JSONValue(decoded->long_name);
                msgPayload["shortname"] = new JSONValue(decoded->short_name);
                msgPayload["hardware"] = new JSONValue(decoded->hw_model);
                jsonObj["payload"] = new JSONValue(msgPayload);
            } else {
                LOG_ERROR("Error decoding protobuf for nodeinfo message!\n");
            }
            break;
        }
        case meshtastic_PortNum_POSITION_APP: {
            msgType = "position";
            meshtastic_Position scratch;
            meshtastic_Position *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_Position_msg, &scratch)) {
                decoded = &scratch;
                if ((int)decoded->time) {
                    msgPayload["time"] = new JSONValue((uint)decoded->time);
                }
                if ((int)decoded->timestamp) {
                    msgPayload["timestamp"] = new JSONValue((uint)decoded->timestamp);
                }
                msgPayload["latitude_i"] = new JSONValue((int)decoded->latitude_i);
                msgPayload["longitude_i"] = new JSONValue((int)decoded->longitude_i);
                if ((int)decoded->altitude) {
                    msgPayload["altitude"] = new JSONValue((int)decoded->altitude);
                }
                if ((int)decoded->ground_speed) {
                    msgPayload["ground_speed"] = new JSONValue((uint)decoded->ground_speed);
                }
                if (int(decoded->ground_track)) {
                    msgPayload["ground_track"] = new JSONValue((uint)decoded->ground_track);
                }
                if (int(decoded->sats_in_view)) {
                    msgPayload["sats_in_view"] = new JSONValue((uint)decoded->sats_in_view);
                }
                if ((int)decoded->PDOP) {
                    msgPayload["PDOP"] = new JSONValue((int)decoded->PDOP);
                }
                if ((int)decoded->HDOP) {
                    msgPayload["HDOP"] = new JSONValue((int)decoded->HDOP);
                }
                if ((int)decoded->VDOP) {
                    msgPayload["VDOP"] = new JSONValue((int)decoded->VDOP);
                }
                jsonObj["payload"] = new JSONValue(msgPayload);
            } else {
                LOG_ERROR("Error decoding protobuf for position message!\n");
            }
            break;
        }
        case meshtastic_PortNum_WAYPOINT_APP: {
            msgType = "position";
            meshtastic_Waypoint scratch;
            meshtastic_Waypoint *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_Waypoint_msg, &scratch)) {
                decoded = &scratch;
                msgPayload["id"] = new JSONValue((uint)decoded->id);
                msgPayload["name"] = new JSONValue(decoded->name);
                msgPayload["description"] = new JSONValue(decoded->description);
                msgPayload["expire"] = new JSONValue((uint)decoded->expire);
                msgPayload["locked_to"] = new JSONValue((uint)decoded->locked_to);
                msgPayload["latitude_i"] = new JSONValue((int)decoded->latitude_i);
                msgPayload["longitude_i"] = new JSONValue((int)decoded->longitude_i);
                jsonObj["payload"] = new JSONValue(msgPayload);
            } else {
                LOG_ERROR("Error decoding protobuf for position message!\n");
            }
            break;
        }
        case meshtastic_PortNum_NEIGHBORINFO_APP: {
            msgType = "neighborinfo";
            meshtastic_NeighborInfo scratch;
            meshtastic_NeighborInfo *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_NeighborInfo_msg,
                                     &scratch)) {
                decoded = &scratch;
                msgPayload["node_id"] = new JSONValue((uint)decoded->node_id);
                msgPayload["node_broadcast_interval_secs"] = new JSONValue((uint)decoded->node_broadcast_interval_secs);
                msgPayload["last_sent_by_id"] = new JSONValue((uint)decoded->last_sent_by_id);
                msgPayload["neighbors_count"] = new JSONValue(decoded->neighbors_count);
                JSONArray neighbors;
                for (uint8_t i = 0; i < decoded->neighbors_count; i++) {
                    JSONObject neighborObj;
                    neighborObj["node_id"] = new JSONValue((uint)decoded->neighbors[i].node_id);
                    neighborObj["snr"] = new JSONValue((int)decoded->neighbors[i].snr);
                    neighbors.push_back(new JSONValue(neighborObj));
                }
                msgPayload["neighbors"] = new JSONValue(neighbors);
                jsonObj["payload"] = new JSONValue(msgPayload);
            } else {
                LOG_ERROR("Error decoding protobuf for neighborinfo message!\n");
            }
            break;
        }
        // add more packet types here if needed
        default:
            break;
        }
    } else {
        LOG_WARN("Couldn't convert encrypted payload of MeshPacket to JSON\n");
    }

    jsonObj["id"] = new JSONValue((uint)mp->id);
    jsonObj["timestamp"] = new JSONValue((uint)mp->rx_time);
    jsonObj["to"] = new JSONValue((uint)mp->to);
    jsonObj["from"] = new JSONValue((uint)mp->from);
    jsonObj["channel"] = new JSONValue((uint)mp->channel);
    jsonObj["type"] = new JSONValue(msgType.c_str());
    jsonObj["sender"] = new JSONValue(owner.id);
    if (mp->rx_rssi != 0)
        jsonObj["rssi"] = new JSONValue((int)mp->rx_rssi);
    if (mp->rx_snr != 0)
        jsonObj["snr"] = new JSONValue((float)mp->rx_snr);

    // serialize and write it to the stream
    JSONValue *value = new JSONValue(jsonObj);
    std::string jsonStr = value->Stringify();

    LOG_INFO("serialized json message: %s\n", jsonStr.c_str());

    delete value;
    return jsonStr;
}