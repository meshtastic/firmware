#include "MQTT.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
#include "mesh/Channels.h"
#include "mesh/Router.h"
#include "mesh/generated/meshtastic/mqtt.pb.h"
#include "mesh/generated/meshtastic/telemetry.pb.h"
#include "modules/RoutingModule.h"
#if defined(ARCH_ESP32)
#include "../mesh/generated/meshtastic/paxcount.pb.h"
#endif
#include "mesh/generated/meshtastic/remote_hardware.pb.h"
#include "sleep.h"
#if HAS_WIFI
#include "mesh/wifi/WiFiAPClient.h"
#include <WiFi.h>
#endif
#include "Default.h"
#include "serialization/JSON.h"
#include "serialization/MeshPacketSerializer.h"
#include <Throttle.h>
#include <assert.h>

const int reconnectMax = 5;

MQTT *mqtt;

static MemoryDynamic<meshtastic_ServiceEnvelope> staticMqttPool;

Allocator<meshtastic_ServiceEnvelope> &mqttPool = staticMqttPool;

// FIXME - this size calculation is super sloppy, but it will go away once we dynamically alloc meshpackets
static uint8_t bytes[meshtastic_MqttClientProxyMessage_size + 30]; // 12 for channel name and 16 for nodeid

static bool isMqttServerAddressPrivate = false;

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
            // check if it is a valid envelope
            JSONObject json;
            json = json_value->AsObject();

            // parse the channel name from the topic string
            // the topic has been checked above for having jsonTopic prefix, so just move past it
            char *ptr = topic + jsonTopic.length();
            ptr = strtok(ptr, "/") ? strtok(ptr, "/") : ptr; // if another "/" was added, parse string up to that character
            meshtastic_Channel sendChannel = channels.getByName(ptr);
            // We allow downlink JSON packets only on a channel named "mqtt"
            if (strncasecmp(channels.getGlobalId(sendChannel.index), Channels::mqttChannel, strlen(Channels::mqttChannel)) == 0 &&
                sendChannel.settings.downlink_enabled) {
                if (isValidJsonEnvelope(json)) {
                    // this is a valid envelope
                    if (json["type"]->AsString().compare("sendtext") == 0 && json["payload"]->IsString()) {
                        std::string jsonPayloadStr = json["payload"]->AsString();
                        LOG_INFO("JSON payload %s, length %u", jsonPayloadStr.c_str(), jsonPayloadStr.length());

                        // construct protobuf data packet using TEXT_MESSAGE, send it to the mesh
                        meshtastic_MeshPacket *p = router->allocForSending();
                        p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
                        if (json.find("channel") != json.end() && json["channel"]->IsNumber() &&
                            (json["channel"]->AsNumber() < channels.getNumChannels()))
                            p->channel = json["channel"]->AsNumber();
                        if (json.find("to") != json.end() && json["to"]->IsNumber())
                            p->to = json["to"]->AsNumber();
                        if (json.find("hopLimit") != json.end() && json["hopLimit"]->IsNumber())
                            p->hop_limit = json["hopLimit"]->AsNumber();
                        if (jsonPayloadStr.length() <= sizeof(p->decoded.payload.bytes)) {
                            memcpy(p->decoded.payload.bytes, jsonPayloadStr.c_str(), jsonPayloadStr.length());
                            p->decoded.payload.size = jsonPayloadStr.length();
                            service->sendToMesh(p, RX_SRC_LOCAL);
                        } else {
                            LOG_WARN("Received MQTT json payload too long, dropping");
                        }
                    } else if (json["type"]->AsString().compare("sendposition") == 0 && json["payload"]->IsObject()) {
                        // invent the "sendposition" type for a valid envelope
                        JSONObject posit;
                        posit = json["payload"]->AsObject(); // get nested JSON Position
                        meshtastic_Position pos = meshtastic_Position_init_default;
                        if (posit.find("latitude_i") != posit.end() && posit["latitude_i"]->IsNumber())
                            pos.latitude_i = posit["latitude_i"]->AsNumber();
                        if (posit.find("longitude_i") != posit.end() && posit["longitude_i"]->IsNumber())
                            pos.longitude_i = posit["longitude_i"]->AsNumber();
                        if (posit.find("altitude") != posit.end() && posit["altitude"]->IsNumber())
                            pos.altitude = posit["altitude"]->AsNumber();
                        if (posit.find("time") != posit.end() && posit["time"]->IsNumber())
                            pos.time = posit["time"]->AsNumber();

                        // construct protobuf data packet using POSITION, send it to the mesh
                        meshtastic_MeshPacket *p = router->allocForSending();
                        p->decoded.portnum = meshtastic_PortNum_POSITION_APP;
                        if (json.find("channel") != json.end() && json["channel"]->IsNumber() &&
                            (json["channel"]->AsNumber() < channels.getNumChannels()))
                            p->channel = json["channel"]->AsNumber();
                        if (json.find("to") != json.end() && json["to"]->IsNumber())
                            p->to = json["to"]->AsNumber();
                        if (json.find("hopLimit") != json.end() && json["hopLimit"]->IsNumber())
                            p->hop_limit = json["hopLimit"]->AsNumber();
                        p->decoded.payload.size =
                            pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
                                               &meshtastic_Position_msg, &pos); // make the Data protobuf from position
                        service->sendToMesh(p, RX_SRC_LOCAL);
                    } else {
                        LOG_DEBUG("JSON Ignoring downlink message with unsupported type.");
                    }
                } else {
                    LOG_ERROR("JSON Received payload on MQTT but not a valid envelope.");
                }
            } else {
                LOG_WARN("JSON downlink received on channel not called 'mqtt' or without downlink enabled.");
            }
        } else {
            // no json, this is an invalid payload
            LOG_ERROR("JSON Received payload on MQTT but not a valid JSON");
        }
        delete json_value;
    } else {
        if (length == 0) {
            LOG_WARN("Empty MQTT payload received, topic %s!", topic);
            return;
        } else if (!pb_decode_from_bytes(payload, length, &meshtastic_ServiceEnvelope_msg, &e)) {
            LOG_ERROR("Invalid MQTT service envelope, topic %s, len %u!", topic, length);
            return;
        } else {
            if (e.channel_id == NULL || e.gateway_id == NULL) {
                LOG_ERROR("Invalid MQTT service envelope, topic %s, len %u!", topic, length);
                return;
            }
            meshtastic_Channel ch = channels.getByName(e.channel_id);
            if (strcmp(e.gateway_id, owner.id) == 0) {
                // Generate an implicit ACK towards ourselves (handled and processed only locally!) for this message.
                // We do this because packets are not rebroadcasted back into MQTT anymore and we assume that at least one node
                // receives it when we get our own packet back. Then we'll stop our retransmissions.
                if (e.packet && isFromUs(e.packet))
                    routingModule->sendAckNak(meshtastic_Routing_Error_NONE, getFrom(e.packet), e.packet->id, ch.index);
                else
                    LOG_INFO("Ignoring downlink message we originally sent.");
            } else {
                // Find channel by channel_id and check downlink_enabled
                if ((strcmp(e.channel_id, "PKI") == 0 && e.packet) ||
                    (strcmp(e.channel_id, channels.getGlobalId(ch.index)) == 0 && e.packet && ch.settings.downlink_enabled)) {
                    LOG_INFO("Received MQTT topic %s, len=%u", topic, length);
                    meshtastic_MeshPacket *p = packetPool.allocCopy(*e.packet);
                    p->via_mqtt = true; // Mark that the packet was received via MQTT

                    if (isFromUs(p)) {
                        LOG_INFO("Ignoring downlink message we originally sent.");
                        packetPool.release(p);
                        return;
                    }
                    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
                        if (moduleConfig.mqtt.encryption_enabled) {
                            LOG_INFO("Ignoring decoded message on MQTT, encryption is enabled.");
                            packetPool.release(p);
                            return;
                        }
                        if (p->decoded.portnum == meshtastic_PortNum_ADMIN_APP) {
                            LOG_INFO("Ignoring decoded admin packet.");
                            packetPool.release(p);
                            return;
                        }
                        p->channel = ch.index;
                    }

                    // PKI messages get accepted even if we can't decrypt
                    if (router && p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag &&
                        strcmp(e.channel_id, "PKI") == 0) {
                        const meshtastic_NodeInfoLite *tx = nodeDB->getMeshNode(getFrom(p));
                        const meshtastic_NodeInfoLite *rx = nodeDB->getMeshNode(p->to);
                        // Only accept PKI messages to us, or if we have both the sender and receiver in our nodeDB, as then it's
                        // likely they discovered each other via a channel we have downlink enabled for
                        if (isToUs(p) || (tx && tx->has_user && rx && rx->has_user))
                            router->enqueueReceivedMessage(p);
                    } else if (router && perhapsDecode(p)) // ignore messages if we don't have the channel key
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

#if HAS_NETWORKING
MQTT::MQTT() : concurrency::OSThread("mqtt"), pubSub(mqttClient), mqttQueue(MAX_MQTT_QUEUE)
#else
MQTT::MQTT() : concurrency::OSThread("mqtt"), mqttQueue(MAX_MQTT_QUEUE)
#endif
{
    if (moduleConfig.mqtt.enabled) {
        LOG_DEBUG("Initializing MQTT");

        assert(!mqtt);
        mqtt = this;

        if (*moduleConfig.mqtt.root) {
            cryptTopic = moduleConfig.mqtt.root + cryptTopic;
            jsonTopic = moduleConfig.mqtt.root + jsonTopic;
            mapTopic = moduleConfig.mqtt.root + mapTopic;
        } else {
            cryptTopic = "msh" + cryptTopic;
            jsonTopic = "msh" + jsonTopic;
            mapTopic = "msh" + mapTopic;
        }

        if (moduleConfig.mqtt.map_reporting_enabled && moduleConfig.mqtt.has_map_report_settings) {
            map_position_precision = Default::getConfiguredOrDefault(moduleConfig.mqtt.map_report_settings.position_precision,
                                                                     default_map_position_precision);
            map_publish_interval_msecs = Default::getConfiguredOrDefaultMs(
                moduleConfig.mqtt.map_report_settings.publish_interval_secs, default_map_publish_interval_secs);
        }

        isMqttServerAddressPrivate = isPrivateIpAddress(moduleConfig.mqtt.address);
        if (isMqttServerAddressPrivate) {
            LOG_INFO("MQTT server is a private IP address.");
        }

#if HAS_NETWORKING
        if (!moduleConfig.mqtt.proxy_to_client_enabled)
            pubSub.setCallback(mqttCallback);
#endif

        if (moduleConfig.mqtt.proxy_to_client_enabled) {
            LOG_INFO("MQTT configured to use client proxy...");
            enabled = true;
            runASAP = true;
            reconnectCount = 0;
            publishNodeInfo();
        }
        // preflightSleepObserver.observe(&preflightSleep);
    } else {
        disable();
    }
}

bool MQTT::isConnectedDirectly()
{
#if HAS_NETWORKING
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
        service->sendMqttMessageToClientProxy(msg);
        return true;
    }
#if HAS_NETWORKING
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
        service->sendMqttMessageToClientProxy(msg);
        return true;
    }
#if HAS_NETWORKING
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
            LOG_INFO("MQTT connecting via client proxy instead...");
            enabled = true;
            runASAP = true;
            reconnectCount = 0;

            publishNodeInfo();
            return; // Don't try to connect directly to the server
        }
#if HAS_NETWORKING
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
#if !defined(CONFIG_IDF_TARGET_ESP32C6) && !defined(RPI_PICO)
        if (moduleConfig.mqtt.tls_enabled) {
            // change default for encrypted to 8883
            try {
                serverPort = 8883;
                wifiSecureClient.setInsecure();

                pubSub.setClient(wifiSecureClient);
                LOG_INFO("Using TLS-encrypted session");
            } catch (const std::exception &e) {
                LOG_ERROR("MQTT ERROR: %s", e.what());
            }
        } else {
            LOG_INFO("Using non-TLS-encrypted session");
            pubSub.setClient(mqttClient);
        }
#else
        pubSub.setClient(mqttClient);
#endif
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

        LOG_INFO("Attempting to connect directly to MQTT server %s, port: %d, username: %s, password: %s", serverAddr, serverPort,
                 mqttUsername, mqttPassword);

        bool connected = pubSub.connect(owner.id, mqttUsername, mqttPassword);
        if (connected) {
            LOG_INFO("MQTT connected");
            enabled = true; // Start running background process again
            runASAP = true;
            reconnectCount = 0;

            publishNodeInfo();
            sendSubscriptions();
        } else {
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
            reconnectCount++;
            LOG_ERROR("Failed to contact MQTT server directly (%d/%d)...", reconnectCount, reconnectMax);
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
#if HAS_NETWORKING
    bool hasDownlink = false;
    size_t numChan = channels.getNumChannels();
    for (size_t i = 0; i < numChan; i++) {
        const auto &ch = channels.getByIndex(i);
        if (ch.settings.downlink_enabled) {
            hasDownlink = true;
            std::string topic = cryptTopic + channels.getGlobalId(i) + "/+";
            LOG_INFO("Subscribing to %s", topic.c_str());
            pubSub.subscribe(topic.c_str(), 1); // FIXME, is QOS 1 right?
#if !defined(ARCH_NRF52) ||                                                                                                      \
    defined(NRF52_USE_JSON) // JSON is not supported on nRF52, see issue #2804 ### Fixed by using ArduinoJSON ###
            if (moduleConfig.mqtt.json_enabled == true) {
                std::string topicDecoded = jsonTopic + channels.getGlobalId(i) + "/+";
                LOG_INFO("Subscribing to %s", topicDecoded.c_str());
                pubSub.subscribe(topicDecoded.c_str(), 1); // FIXME, is QOS 1 right?
            }
#endif // ARCH_NRF52 NRF52_USE_JSON
        }
    }
#if !MESHTASTIC_EXCLUDE_PKI
    if (hasDownlink) {
        std::string topic = cryptTopic + "PKI/+";
        LOG_INFO("Subscribing to %s", topic.c_str());
        pubSub.subscribe(topic.c_str(), 1);
    }
#endif
#endif
}

bool MQTT::wantsLink() const
{
    bool hasChannelorMapReport =
        moduleConfig.mqtt.enabled && (moduleConfig.mqtt.map_reporting_enabled || channels.anyMqttEnabled());

    if (hasChannelorMapReport && moduleConfig.mqtt.proxy_to_client_enabled)
        return true;

#if HAS_WIFI
    return hasChannelorMapReport && WiFi.isConnected();
#endif
#if HAS_ETHERNET
    return hasChannelorMapReport && Ethernet.linkStatus() == LinkON;
#endif
    return false;
}

int32_t MQTT::runOnce()
{
#if HAS_NETWORKING
    if (!moduleConfig.mqtt.enabled || !(moduleConfig.mqtt.map_reporting_enabled || channels.anyMqttEnabled()))
        return disable();

    bool wantConnection = wantsLink();

    perhapsReportToMap();

    // If connected poll rapidly, otherwise only occasionally check for a wifi connection change and ability to contact server
    if (moduleConfig.mqtt.proxy_to_client_enabled) {
        publishQueuedMessages();
        return 200;
    }

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
            LOG_INFO("MQTT link not needed, dropping");
            pubSub.disconnect();
        }

        powerFSM.trigger(EVENT_CONTACT_FROM_PHONE); // Suppress entering light sleep (because that would turn off bluetooth)
        return 20;
    }
#endif
    return 30000;
}

void MQTT::publishNodeInfo()
{
    // TODO: NodeInfo broadcast over MQTT only (NODENUM_BROADCAST_NO_LORA)
}
void MQTT::publishQueuedMessages()
{
    if (!mqttQueue.isEmpty()) {
        LOG_DEBUG("Publishing enqueued MQTT message");
        meshtastic_ServiceEnvelope *env = mqttQueue.dequeuePtr(0);
        size_t numBytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_ServiceEnvelope_msg, env);
        std::string topic;
        if (env->packet->pki_encrypted) {
            topic = cryptTopic + "PKI/" + owner.id;
        } else {
            topic = cryptTopic + env->channel_id + "/" + owner.id;
        }
        LOG_INFO("publish %s, %u bytes from queue", topic.c_str(), numBytes);

        publish(topic.c_str(), bytes, numBytes, false);

#if !defined(ARCH_NRF52) ||                                                                                                      \
    defined(NRF52_USE_JSON) // JSON is not supported on nRF52, see issue #2804 ### Fixed by using ArduinoJson ###
        if (moduleConfig.mqtt.json_enabled) {
            // handle json topic
            auto jsonString = MeshPacketSerializer::JsonSerialize(env->packet);
            if (jsonString.length() != 0) {
                std::string topicJson;
                if (env->packet->pki_encrypted) {
                    topicJson = jsonTopic + "PKI/" + owner.id;
                } else {
                    topicJson = jsonTopic + env->channel_id + "/" + owner.id;
                }
                LOG_INFO("JSON publish message to %s, %u bytes: %s", topicJson.c_str(), jsonString.length(), jsonString.c_str());
                publish(topicJson.c_str(), jsonString.c_str(), false);
            }
        }
#endif // ARCH_NRF52 NRF52_USE_JSON
        mqttPool.release(env);
    }
}

void MQTT::onSend(const meshtastic_MeshPacket &mp_encrypted, const meshtastic_MeshPacket &mp_decoded, ChannelIndex chIndex)
{
    if (mp_encrypted.via_mqtt)
        return; // Don't send messages that came from MQTT back into MQTT
    bool uplinkEnabled = false;
    for (int i = 0; i <= 7; i++) {
        if (channels.getByIndex(i).settings.uplink_enabled)
            uplinkEnabled = true;
    }
    if (!uplinkEnabled)
        return; // no channels have an uplink enabled
    auto &ch = channels.getByIndex(chIndex);

    // mp_decoded will not be decoded when it's PKI encrypted and not directed to us
    if (mp_decoded.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        // check for the lowest bit of the data bitfield set false, and the use of one of the default keys.
        if (!isFromUs(&mp_decoded) && !isMqttServerAddressPrivate && mp_decoded.decoded.has_bitfield &&
            !(mp_decoded.decoded.bitfield & BITFIELD_OK_TO_MQTT_MASK) &&
            (ch.settings.psk.size < 2 || (ch.settings.psk.size == 16 && memcmp(ch.settings.psk.bytes, defaultpsk, 16)) ||
             (ch.settings.psk.size == 32 && memcmp(ch.settings.psk.bytes, eventpsk, 32)))) {
            LOG_INFO("MQTT onSend - Not forwarding packet due to DontMqttMeBro flag");
            return;
        }

        if (strcmp(moduleConfig.mqtt.address, default_mqtt_address) == 0 &&
            (mp_decoded.decoded.portnum == meshtastic_PortNum_RANGE_TEST_APP ||
             mp_decoded.decoded.portnum == meshtastic_PortNum_DETECTION_SENSOR_APP)) {
            LOG_DEBUG("MQTT onSend - Ignoring range test or detection sensor message on public mqtt");
            return;
        }
    }
    // Either encrypted packet (we couldn't decrypt) is marked as pki_encrypted, or we could decode the PKI encrypted packet
    bool isPKIEncrypted = mp_encrypted.pki_encrypted || mp_decoded.pki_encrypted;
    // If it was to a channel, check uplink enabled, else must be pki_encrypted
    if ((ch.settings.uplink_enabled && !isPKIEncrypted) || isPKIEncrypted) {
        const char *channelId = isPKIEncrypted ? "PKI" : channels.getGlobalId(chIndex);

        meshtastic_ServiceEnvelope *env = mqttPool.allocZeroed();
        env->channel_id = (char *)channelId;
        env->gateway_id = owner.id;

        LOG_DEBUG("MQTT onSend - Publishing ");
        if (moduleConfig.mqtt.encryption_enabled) {
            env->packet = (meshtastic_MeshPacket *)&mp_encrypted;
            LOG_DEBUG("encrypted message");
        } else if (mp_decoded.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
            env->packet = (meshtastic_MeshPacket *)&mp_decoded;
            LOG_DEBUG("portnum %i message", env->packet->decoded.portnum);
        } else {
            LOG_DEBUG("nothing, pkt not decrypted");
            return; // Don't upload a still-encrypted PKI packet if not encryption_enabled
        }

        if (moduleConfig.mqtt.proxy_to_client_enabled || this->isConnectedDirectly()) {
            size_t numBytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_ServiceEnvelope_msg, env);
            std::string topic = cryptTopic + channelId + "/" + owner.id;
            LOG_DEBUG("MQTT Publish %s, %u bytes", topic.c_str(), numBytes);

            publish(topic.c_str(), bytes, numBytes, false);

#if !defined(ARCH_NRF52) ||                                                                                                      \
    defined(NRF52_USE_JSON) // JSON is not supported on nRF52, see issue #2804 ### Fixed by using ArduinoJson ###
            if (moduleConfig.mqtt.json_enabled) {
                // handle json topic
                auto jsonString = MeshPacketSerializer::JsonSerialize((meshtastic_MeshPacket *)&mp_decoded);
                if (jsonString.length() != 0) {
                    std::string topicJson = jsonTopic + channelId + "/" + owner.id;
                    LOG_INFO("JSON publish message to %s, %u bytes: %s", topicJson.c_str(), jsonString.length(),
                             jsonString.c_str());
                    publish(topicJson.c_str(), jsonString.c_str(), false);
                }
            }
#endif // ARCH_NRF52 NRF52_USE_JSON
        } else {
            LOG_INFO("MQTT not connected, queueing packet");
            if (mqttQueue.numFree() == 0) {
                LOG_WARN("NOTE: MQTT queue is full, discarding oldest");
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

void MQTT::perhapsReportToMap()
{
    if (!moduleConfig.mqtt.map_reporting_enabled || !(moduleConfig.mqtt.proxy_to_client_enabled || isConnectedDirectly()))
        return;

    if (Throttle::isWithinTimespanMs(last_report_to_map, map_publish_interval_msecs)) {
        return;
    } else {
        if (map_position_precision == 0 || (localPosition.latitude_i == 0 && localPosition.longitude_i == 0)) {
            last_report_to_map = millis();
            if (map_position_precision == 0)
                LOG_WARN("MQTT Map reporting is enabled, but precision is 0");
            if (localPosition.latitude_i == 0 && localPosition.longitude_i == 0)
                LOG_WARN("MQTT Map reporting is enabled, but no position available.");
            return;
        }

        // Allocate ServiceEnvelope and fill it
        meshtastic_ServiceEnvelope *se = mqttPool.allocZeroed();
        se->channel_id = (char *)channels.getGlobalId(channels.getPrimaryIndex()); // Use primary channel as the channel_id
        se->gateway_id = owner.id;

        // Allocate MeshPacket and fill it
        meshtastic_MeshPacket *mp = packetPool.allocZeroed();
        mp->which_payload_variant = meshtastic_MeshPacket_decoded_tag;
        mp->from = nodeDB->getNodeNum();
        mp->to = NODENUM_BROADCAST;
        mp->decoded.portnum = meshtastic_PortNum_MAP_REPORT_APP;

        // Fill MapReport message
        meshtastic_MapReport mapReport = meshtastic_MapReport_init_default;
        memcpy(mapReport.long_name, owner.long_name, sizeof(owner.long_name));
        memcpy(mapReport.short_name, owner.short_name, sizeof(owner.short_name));
        mapReport.role = config.device.role;
        mapReport.hw_model = owner.hw_model;
        strncpy(mapReport.firmware_version, optstr(APP_VERSION), sizeof(mapReport.firmware_version));
        mapReport.region = config.lora.region;
        mapReport.modem_preset = config.lora.modem_preset;
        mapReport.has_default_channel = channels.hasDefaultChannel();

        // Set position with precision (same as in PositionModule)
        if (map_position_precision < 32 && map_position_precision > 0) {
            mapReport.latitude_i = localPosition.latitude_i & (UINT32_MAX << (32 - map_position_precision));
            mapReport.longitude_i = localPosition.longitude_i & (UINT32_MAX << (32 - map_position_precision));
            mapReport.latitude_i += (1 << (31 - map_position_precision));
            mapReport.longitude_i += (1 << (31 - map_position_precision));
        } else {
            mapReport.latitude_i = localPosition.latitude_i;
            mapReport.longitude_i = localPosition.longitude_i;
        }
        mapReport.altitude = localPosition.altitude;
        mapReport.position_precision = map_position_precision;

        mapReport.num_online_local_nodes = nodeDB->getNumOnlineMeshNodes(true);

        // Encode MapReport message and set it to MeshPacket in ServiceEnvelope
        mp->decoded.payload.size = pb_encode_to_bytes(mp->decoded.payload.bytes, sizeof(mp->decoded.payload.bytes),
                                                      &meshtastic_MapReport_msg, &mapReport);
        se->packet = mp;

        size_t numBytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_ServiceEnvelope_msg, se);

        LOG_INFO("MQTT Publish map report to %s", mapTopic.c_str());
        publish(mapTopic.c_str(), bytes, numBytes, false);

        // Release the allocated memory for ServiceEnvelope and MeshPacket
        mqttPool.release(se);
        packetPool.release(mp);

        // Update the last report time
        last_report_to_map = millis();
    }
}

bool MQTT::isValidJsonEnvelope(JSONObject &json)
{
    // if "sender" is provided, avoid processing packets we uplinked
    return (json.find("sender") != json.end() ? (json["sender"]->AsString().compare(owner.id) != 0) : true) &&
           (json.find("hopLimit") != json.end() ? json["hopLimit"]->IsNumber() : true) && // hop limit should be a number
           (json.find("from") != json.end()) && json["from"]->IsNumber() &&
           (json["from"]->AsNumber() == nodeDB->getNodeNum()) &&            // only accept message if the "from" is us
           (json.find("type") != json.end()) && json["type"]->IsString() && // should specify a type
           (json.find("payload") != json.end());                            // should have a payload
}

bool MQTT::isPrivateIpAddress(const char address[])
{
    // Min. length like 10.0.0.0 (8), max like 192.168.255.255:65535 (21)
    size_t length = strlen(address);
    if (length < 8 || length > 21) {
        return false;
    }

    // Ensure the address contains only digits and dots and maybe a colon.
    // Some limited validation is done.
    // Even if it's not a valid IP address, we will know it's not a domain.
    bool hasColon = false;
    int numDots = 0;
    for (int i = 0; i < length; i++) {
        if (!isdigit(address[i]) && address[i] != '.' && address[i] != ':') {
            return false;
        }

        // Dots can't be the first character, immediately follow another dot,
        // occur more than 3 times, or occur after a colon.
        if (address[i] == '.') {
            if (++numDots > 3 || i == 0 || address[i - 1] == '.' || hasColon) {
                return false;
            }
        }
        // There can only be a single colon, and it can only occur after 3 dots
        else if (address[i] == ':') {
            if (hasColon || numDots < 3) {
                return false;
            }

            hasColon = true;
        }
    }

    // Final validation for IPv4 address and port format.
    // Note that the values of octets haven't been tested, only the address format.
    if (numDots != 3) {
        return false;
    }

    // Check the easy ones first.
    if (strcmp(address, "127.0.0.1") == 0 || strncmp(address, "10.", 3) == 0 || strncmp(address, "192.168", 7) == 0) {
        return true;
    }

    // See if it's definitely not a 172 address.
    if (strncmp(address, "172", 3) != 0) {
        return false;
    }

    // We know it's a 172 address, now see if the second octet is 2 digits.
    if (address[6] != '.') {
        return false;
    }

    // Copy the second octet into a secondary buffer we can null-terminate and parse.
    char octet2[3];
    strncpy(octet2, address + 4, 2);
    octet2[2] = 0;

    int octet2Num = atoi(octet2);
    return octet2Num >= 16 && octet2Num <= 31;
}