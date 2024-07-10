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
                        LOG_INFO("JSON payload %s, length %u\n", jsonPayloadStr.c_str(), jsonPayloadStr.length());

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
                            service.sendToMesh(p, RX_SRC_LOCAL);
                        } else {
                            LOG_WARN("Received MQTT json payload too long, dropping\n");
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
                        service.sendToMesh(p, RX_SRC_LOCAL);
                    } else {
                        LOG_DEBUG("JSON Ignoring downlink message with unsupported type.\n");
                    }
                } else {
                    LOG_ERROR("JSON Received payload on MQTT but not a valid envelope.\n");
                }
            } else {
                LOG_WARN("JSON downlink received on channel not called 'mqtt' or without downlink enabled.\n");
            }
        } else {
            // no json, this is an invalid payload
            LOG_ERROR("JSON Received payload on MQTT but not a valid JSON\n");
        }
        delete json_value;
    } else {
        if (length == 0) {
            LOG_WARN("Empty MQTT payload received, topic %s!\n", topic);
            return;
        } else if (!pb_decode_from_bytes(payload, length, &meshtastic_ServiceEnvelope_msg, &e)) {
            LOG_ERROR("Invalid MQTT service envelope, topic %s, len %u!\n", topic, length);
            return;
        } else {
            meshtastic_Channel ch = channels.getByName(e.channel_id);
            if (strcmp(e.gateway_id, owner.id) == 0) {
                // Generate an implicit ACK towards ourselves (handled and processed only locally!) for this message.
                // We do this because packets are not rebroadcasted back into MQTT anymore and we assume that at least one node
                // receives it when we get our own packet back. Then we'll stop our retransmissions.
                if (e.packet && getFrom(e.packet) == nodeDB->getNodeNum())
                    routingModule->sendAckNak(meshtastic_Routing_Error_NONE, getFrom(e.packet), e.packet->id, ch.index);
                else
                    LOG_INFO("Ignoring downlink message we originally sent.\n");
            } else {
                // Find channel by channel_id and check downlink_enabled
                if (strcmp(e.channel_id, channels.getGlobalId(ch.index)) == 0 && e.packet && ch.settings.downlink_enabled) {
                    LOG_INFO("Received MQTT topic %s, len=%u\n", topic, length);
                    meshtastic_MeshPacket *p = packetPool.allocCopy(*e.packet);
                    p->via_mqtt = true; // Mark that the packet was received via MQTT

                    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
                        p->channel = ch.index;
                    }

                    // ignore messages if we don't have the channel key
                    if (router && perhapsDecode(p))
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
        LOG_DEBUG("Initializing MQTT\n");

        assert(!mqtt);
        mqtt = this;

        if (*moduleConfig.mqtt.root) {
            statusTopic = moduleConfig.mqtt.root + statusTopic;
            cryptTopic = moduleConfig.mqtt.root + cryptTopic;
            jsonTopic = moduleConfig.mqtt.root + jsonTopic;
            mapTopic = moduleConfig.mqtt.root + mapTopic;
        } else {
            statusTopic = "msh" + statusTopic;
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

#if HAS_NETWORKING
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
        service.sendMqttMessageToClientProxy(msg);
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
        service.sendMqttMessageToClientProxy(msg);
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
            LOG_INFO("MQTT connecting via client proxy instead...\n");
            enabled = true;
            runASAP = true;
            reconnectCount = 0;

            publishStatus();
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
#if HAS_NETWORKING
    size_t numChan = channels.getNumChannels();
    for (size_t i = 0; i < numChan; i++) {
        const auto &ch = channels.getByIndex(i);
        if (ch.settings.downlink_enabled) {
            std::string topic = cryptTopic + channels.getGlobalId(i) + "/#";
            LOG_INFO("Subscribing to %s\n", topic.c_str());
            pubSub.subscribe(topic.c_str(), 1); // FIXME, is QOS 1 right?
#ifndef ARCH_NRF52                              // JSON is not supported on nRF52, see issue #2804
            if (moduleConfig.mqtt.json_enabled == true) {
                std::string topicDecoded = jsonTopic + channels.getGlobalId(i) + "/#";
                LOG_INFO("Subscribing to %s\n", topicDecoded.c_str());
                pubSub.subscribe(topicDecoded.c_str(), 1); // FIXME, is QOS 1 right?
            }
#endif // ARCH_NRF52
        }
    }
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

#ifndef ARCH_NRF52 // JSON is not supported on nRF52, see issue #2804
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
#endif // ARCH_NRF52
        mqttPool.release(env);
    }
}

void MQTT::onSend(const meshtastic_MeshPacket &mp, const meshtastic_MeshPacket &mp_decoded, ChannelIndex chIndex)
{
    if (mp.via_mqtt)
        return; // Don't send messages that came from MQTT back into MQTT

    auto &ch = channels.getByIndex(chIndex);

    if (mp_decoded.which_payload_variant != meshtastic_MeshPacket_decoded_tag) {
        LOG_CRIT("MQTT::onSend(): mp_decoded isn't actually decoded\n");
        return;
    }

    if (strcmp(moduleConfig.mqtt.address, default_mqtt_address) == 0 &&
        (mp_decoded.decoded.portnum == meshtastic_PortNum_RANGE_TEST_APP ||
         mp_decoded.decoded.portnum == meshtastic_PortNum_DETECTION_SENSOR_APP)) {
        LOG_DEBUG("MQTT onSend - Ignoring range test or detection sensor message on public mqtt\n");
        return;
    }

    if (ch.settings.uplink_enabled) {
        const char *channelId = channels.getGlobalId(chIndex); // FIXME, for now we just use the human name for the channel

        meshtastic_ServiceEnvelope *env = mqttPool.allocZeroed();
        env->channel_id = (char *)channelId;
        env->gateway_id = owner.id;

        LOG_DEBUG("MQTT onSend - Publishing ");
        if (moduleConfig.mqtt.encryption_enabled) {
            env->packet = (meshtastic_MeshPacket *)&mp;
            LOG_DEBUG("encrypted message\n");
        } else {
            env->packet = (meshtastic_MeshPacket *)&mp_decoded;
            LOG_DEBUG("portnum %i message\n", env->packet->decoded.portnum);
        }

        if (moduleConfig.mqtt.proxy_to_client_enabled || this->isConnectedDirectly()) {
            // FIXME - this size calculation is super sloppy, but it will go away once we dynamically alloc meshpackets
            static uint8_t bytes[meshtastic_MeshPacket_size + 64];
            size_t numBytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_ServiceEnvelope_msg, env);

            std::string topic = cryptTopic + channelId + "/" + owner.id;
            LOG_DEBUG("MQTT Publish %s, %u bytes\n", topic.c_str(), numBytes);

            publish(topic.c_str(), bytes, numBytes, false);

#ifndef ARCH_NRF52 // JSON is not supported on nRF52, see issue #2804
            if (moduleConfig.mqtt.json_enabled) {
                // handle json topic
                auto jsonString = this->meshPacketToJson((meshtastic_MeshPacket *)&mp_decoded);
                if (jsonString.length() != 0) {
                    std::string topicJson = jsonTopic + channelId + "/" + owner.id;
                    LOG_INFO("JSON publish message to %s, %u bytes: %s\n", topicJson.c_str(), jsonString.length(),
                             jsonString.c_str());
                    publish(topicJson.c_str(), jsonString.c_str(), false);
                }
            }
#endif // ARCH_NRF52
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

void MQTT::perhapsReportToMap()
{
    if (!moduleConfig.mqtt.map_reporting_enabled || !(moduleConfig.mqtt.proxy_to_client_enabled || isConnectedDirectly()))
        return;

    if (millis() - last_report_to_map < map_publish_interval_msecs) {
        return;
    } else {
        if (map_position_precision == 0 || (localPosition.latitude_i == 0 && localPosition.longitude_i == 0)) {
            last_report_to_map = millis();
            if (map_position_precision == 0)
                LOG_WARN("MQTT Map reporting is enabled, but precision is 0\n");
            if (localPosition.latitude_i == 0 && localPosition.longitude_i == 0)
                LOG_WARN("MQTT Map reporting is enabled, but no position available.\n");
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

        // FIXME - this size calculation is super sloppy, but it will go away once we dynamically alloc meshpackets
        static uint8_t bytes[meshtastic_MeshPacket_size + 64];
        size_t numBytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_ServiceEnvelope_msg, se);

        LOG_INFO("MQTT Publish map report to %s\n", mapTopic.c_str());
        publish(mapTopic.c_str(), bytes, numBytes, false);

        // Release the allocated memory for ServiceEnvelope and MeshPacket
        mqttPool.release(se);
        packetPool.release(mp);

        // Update the last report time
        last_report_to_map = millis();
    }
}

// converts a downstream packet into a json message
std::string MQTT::meshPacketToJson(meshtastic_MeshPacket *mp)
{
    // the created jsonObj is immutable after creation, so
    // we need to do the heavy lifting before assembling it.
    std::string msgType;
    JSONObject jsonObj;

    if (mp->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        JSONObject msgPayload;
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
                    msgPayload["battery_level"] = new JSONValue((unsigned int)decoded->variant.device_metrics.battery_level);
                    msgPayload["voltage"] = new JSONValue(decoded->variant.device_metrics.voltage);
                    msgPayload["channel_utilization"] = new JSONValue(decoded->variant.device_metrics.channel_utilization);
                    msgPayload["air_util_tx"] = new JSONValue(decoded->variant.device_metrics.air_util_tx);
                    msgPayload["uptime_seconds"] = new JSONValue((unsigned int)decoded->variant.device_metrics.uptime_seconds);
                } else if (decoded->which_variant == meshtastic_Telemetry_environment_metrics_tag) {
                    msgPayload["temperature"] = new JSONValue(decoded->variant.environment_metrics.temperature);
                    msgPayload["relative_humidity"] = new JSONValue(decoded->variant.environment_metrics.relative_humidity);
                    msgPayload["barometric_pressure"] = new JSONValue(decoded->variant.environment_metrics.barometric_pressure);
                    msgPayload["gas_resistance"] = new JSONValue(decoded->variant.environment_metrics.gas_resistance);
                    msgPayload["voltage"] = new JSONValue(decoded->variant.environment_metrics.voltage);
                    msgPayload["current"] = new JSONValue(decoded->variant.environment_metrics.current);
                    msgPayload["lux"] = new JSONValue(decoded->variant.environment_metrics.lux);
                    msgPayload["white_lux"] = new JSONValue(decoded->variant.environment_metrics.white_lux);
                    msgPayload["iaq"] = new JSONValue((uint)decoded->variant.environment_metrics.iaq);
                    msgPayload["wind_speed"] = new JSONValue((uint)decoded->variant.environment_metrics.wind_speed);
                    msgPayload["wind_direction"] = new JSONValue((uint)decoded->variant.environment_metrics.wind_direction);
                } else if (decoded->which_variant == meshtastic_Telemetry_power_metrics_tag) {
                    msgPayload["voltage_ch1"] = new JSONValue(decoded->variant.power_metrics.ch1_voltage);
                    msgPayload["current_ch1"] = new JSONValue(decoded->variant.power_metrics.ch1_current);
                    msgPayload["voltage_ch2"] = new JSONValue(decoded->variant.power_metrics.ch2_voltage);
                    msgPayload["current_ch2"] = new JSONValue(decoded->variant.power_metrics.ch2_current);
                    msgPayload["voltage_ch3"] = new JSONValue(decoded->variant.power_metrics.ch3_voltage);
                    msgPayload["current_ch3"] = new JSONValue(decoded->variant.power_metrics.ch3_current);
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
                    msgPayload["time"] = new JSONValue((unsigned int)decoded->time);
                }
                if ((int)decoded->timestamp) {
                    msgPayload["timestamp"] = new JSONValue((unsigned int)decoded->timestamp);
                }
                msgPayload["latitude_i"] = new JSONValue((int)decoded->latitude_i);
                msgPayload["longitude_i"] = new JSONValue((int)decoded->longitude_i);
                if ((int)decoded->altitude) {
                    msgPayload["altitude"] = new JSONValue((int)decoded->altitude);
                }
                if ((int)decoded->ground_speed) {
                    msgPayload["ground_speed"] = new JSONValue((unsigned int)decoded->ground_speed);
                }
                if (int(decoded->ground_track)) {
                    msgPayload["ground_track"] = new JSONValue((unsigned int)decoded->ground_track);
                }
                if (int(decoded->sats_in_view)) {
                    msgPayload["sats_in_view"] = new JSONValue((unsigned int)decoded->sats_in_view);
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
                if ((int)decoded->precision_bits) {
                    msgPayload["precision_bits"] = new JSONValue((int)decoded->precision_bits);
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
                msgPayload["id"] = new JSONValue((unsigned int)decoded->id);
                msgPayload["name"] = new JSONValue(decoded->name);
                msgPayload["description"] = new JSONValue(decoded->description);
                msgPayload["expire"] = new JSONValue((unsigned int)decoded->expire);
                msgPayload["locked_to"] = new JSONValue((unsigned int)decoded->locked_to);
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
                msgPayload["node_id"] = new JSONValue((unsigned int)decoded->node_id);
                msgPayload["node_broadcast_interval_secs"] = new JSONValue((unsigned int)decoded->node_broadcast_interval_secs);
                msgPayload["last_sent_by_id"] = new JSONValue((unsigned int)decoded->last_sent_by_id);
                msgPayload["neighbors_count"] = new JSONValue(decoded->neighbors_count);
                JSONArray neighbors;
                for (uint8_t i = 0; i < decoded->neighbors_count; i++) {
                    JSONObject neighborObj;
                    neighborObj["node_id"] = new JSONValue((unsigned int)decoded->neighbors[i].node_id);
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
        case meshtastic_PortNum_TRACEROUTE_APP: {
            if (mp->decoded.request_id) { // Only report the traceroute response
                msgType = "traceroute";
                meshtastic_RouteDiscovery scratch;
                meshtastic_RouteDiscovery *decoded = NULL;
                memset(&scratch, 0, sizeof(scratch));
                if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_RouteDiscovery_msg,
                                         &scratch)) {
                    decoded = &scratch;
                    JSONArray route; // Route this message took
                    // Lambda function for adding a long name to the route
                    auto addToRoute = [](JSONArray *route, NodeNum num) {
                        char long_name[40] = "Unknown";
                        meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(num);
                        bool name_known = node ? node->has_user : false;
                        if (name_known)
                            memcpy(long_name, node->user.long_name, sizeof(long_name));
                        route->push_back(new JSONValue(long_name));
                    };
                    addToRoute(&route, mp->to); // Started at the original transmitter (destination of response)
                    for (uint8_t i = 0; i < decoded->route_count; i++) {
                        addToRoute(&route, decoded->route[i]);
                    }
                    addToRoute(&route, mp->from); // Ended at the original destination (source of response)

                    msgPayload["route"] = new JSONValue(route);
                    jsonObj["payload"] = new JSONValue(msgPayload);
                } else {
                    LOG_ERROR("Error decoding protobuf for traceroute message!\n");
                }
            }
            break;
        }
        case meshtastic_PortNum_DETECTION_SENSOR_APP: {
            msgType = "detection";
            char payloadStr[(mp->decoded.payload.size) + 1];
            memcpy(payloadStr, mp->decoded.payload.bytes, mp->decoded.payload.size);
            payloadStr[mp->decoded.payload.size] = 0; // null terminated string
            msgPayload["text"] = new JSONValue(payloadStr);
            jsonObj["payload"] = new JSONValue(msgPayload);
            break;
        }
#ifdef ARCH_ESP32
        case meshtastic_PortNum_PAXCOUNTER_APP: {
            msgType = "paxcounter";
            meshtastic_Paxcount scratch;
            meshtastic_Paxcount *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_Paxcount_msg, &scratch)) {
                decoded = &scratch;
                msgPayload["wifi_count"] = new JSONValue((unsigned int)decoded->wifi);
                msgPayload["ble_count"] = new JSONValue((unsigned int)decoded->ble);
                msgPayload["uptime"] = new JSONValue((unsigned int)decoded->uptime);
                jsonObj["payload"] = new JSONValue(msgPayload);
            } else {
                LOG_ERROR("Error decoding protobuf for Paxcount message!\n");
            }
            break;
        }
#endif
        case meshtastic_PortNum_REMOTE_HARDWARE_APP: {
            meshtastic_HardwareMessage scratch;
            meshtastic_HardwareMessage *decoded = NULL;
            memset(&scratch, 0, sizeof(scratch));
            if (pb_decode_from_bytes(mp->decoded.payload.bytes, mp->decoded.payload.size, &meshtastic_HardwareMessage_msg,
                                     &scratch)) {
                decoded = &scratch;
                if (decoded->type == meshtastic_HardwareMessage_Type_GPIOS_CHANGED) {
                    msgType = "gpios_changed";
                    msgPayload["gpio_value"] = new JSONValue((unsigned int)decoded->gpio_value);
                    jsonObj["payload"] = new JSONValue(msgPayload);
                } else if (decoded->type == meshtastic_HardwareMessage_Type_READ_GPIOS_REPLY) {
                    msgType = "gpios_read_reply";
                    msgPayload["gpio_value"] = new JSONValue((unsigned int)decoded->gpio_value);
                    msgPayload["gpio_mask"] = new JSONValue((unsigned int)decoded->gpio_mask);
                    jsonObj["payload"] = new JSONValue(msgPayload);
                }
            } else {
                LOG_ERROR("Error decoding protobuf for RemoteHardware message!\n");
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

    jsonObj["id"] = new JSONValue((unsigned int)mp->id);
    jsonObj["timestamp"] = new JSONValue((unsigned int)mp->rx_time);
    jsonObj["to"] = new JSONValue((unsigned int)mp->to);
    jsonObj["from"] = new JSONValue((unsigned int)mp->from);
    jsonObj["channel"] = new JSONValue((unsigned int)mp->channel);
    jsonObj["type"] = new JSONValue(msgType.c_str());
    jsonObj["sender"] = new JSONValue(owner.id);
    if (mp->rx_rssi != 0)
        jsonObj["rssi"] = new JSONValue((int)mp->rx_rssi);
    if (mp->rx_snr != 0)
        jsonObj["snr"] = new JSONValue((float)mp->rx_snr);
    if (mp->hop_start != 0 && mp->hop_limit <= mp->hop_start) {
        jsonObj["hops_away"] = new JSONValue((unsigned int)(mp->hop_start - mp->hop_limit));
        jsonObj["hop_start"] = new JSONValue((unsigned int)(mp->hop_start));
    }

    // serialize and write it to the stream
    JSONValue *value = new JSONValue(jsonObj);
    std::string jsonStr = value->Stringify();

    LOG_INFO("serialized json message: %s\n", jsonStr.c_str());

    delete value;
    return jsonStr;
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