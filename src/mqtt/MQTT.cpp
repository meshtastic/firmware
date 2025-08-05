#include "MQTT.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "ServiceEnvelope.h"
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
#if HAS_ETHERNET && defined(USE_WS5500)
#include <ETHClass2.h>
#define ETH ETH2
#endif // HAS_ETHERNET
#include "Default.h"
#if !defined(ARCH_NRF52) || NRF52_USE_JSON
#include "serialization/JSON.h"
#include "serialization/MeshPacketSerializer.h"
#endif
#include <Throttle.h>
#include <assert.h>
#include <utility>

#include <IPAddress.h>
#if defined(ARCH_PORTDUINO)
#include <netinet/in.h>
#elif !defined(ntohl)
#include <machine/endian.h>
#define ntohl __ntohl
#endif
#include <RTC.h>

MQTT *mqtt;

namespace
{
constexpr int reconnectMax = 5;

// FIXME - this size calculation is super sloppy, but it will go away once we dynamically alloc meshpackets
static uint8_t bytes[meshtastic_MqttClientProxyMessage_size + 30]; // 12 for channel name and 16 for nodeid

static bool isMqttServerAddressPrivate = false;

inline void onReceiveProto(char *topic, byte *payload, size_t length)
{
    const DecodedServiceEnvelope e(payload, length);
    if (!e.validDecode || e.channel_id == NULL || e.gateway_id == NULL || e.packet == NULL) {
        LOG_ERROR("Invalid MQTT service envelope, topic %s, len %u!", topic, length);
        return;
    }
    const meshtastic_Channel &ch = channels.getByName(e.channel_id);
    if (strcmp(e.gateway_id, owner.id) == 0) {
        // Generate an implicit ACK towards ourselves (handled and processed only locally!) for this message.
        // We do this because packets are not rebroadcasted back into MQTT anymore and we assume that at least one node
        // receives it when we get our own packet back. Then we'll stop our retransmissions.
        if (isFromUs(e.packet))
            routingModule->sendAckNak(meshtastic_Routing_Error_NONE, getFrom(e.packet), e.packet->id, ch.index);
        else
            LOG_INFO("Ignore downlink message we originally sent");
        return;
    }
    if (isFromUs(e.packet)) {
        LOG_INFO("Ignore downlink message we originally sent");
        return;
    }

    // Find channel by channel_id and check downlink_enabled
    if (!(strcmp(e.channel_id, "PKI") == 0 ||
          (strcmp(e.channel_id, channels.getGlobalId(ch.index)) == 0 && ch.settings.downlink_enabled))) {
        return;
    }
    LOG_INFO("Received MQTT topic %s, len=%u", topic, length);
    if (e.packet->hop_limit > HOP_MAX || e.packet->hop_start > HOP_MAX) {
        LOG_INFO("Invalid hop_limit(%u) or hop_start(%u)", e.packet->hop_limit, e.packet->hop_start);
        return;
    }

    UniquePacketPoolPacket p = packetPool.allocUniqueZeroed();
    p->from = e.packet->from;
    p->to = e.packet->to;
    p->id = e.packet->id;
    p->channel = e.packet->channel;
    p->hop_limit = e.packet->hop_limit;
    p->hop_start = e.packet->hop_start;
    p->want_ack = e.packet->want_ack;
    p->via_mqtt = true; // Mark that the packet was received via MQTT
    p->which_payload_variant = e.packet->which_payload_variant;
    memcpy(&p->decoded, &e.packet->decoded, std::max(sizeof(p->decoded), sizeof(p->encrypted)));

    if (p->which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        if (moduleConfig.mqtt.encryption_enabled) {
            LOG_INFO("Ignore decoded message on MQTT, encryption is enabled");
            return;
        }
        if (p->decoded.portnum == meshtastic_PortNum_ADMIN_APP) {
            LOG_INFO("Ignore decoded admin packet");
            return;
        }
        p->channel = ch.index;
    }

    // PKI messages get accepted even if we can't decrypt
    if (router && p->which_payload_variant == meshtastic_MeshPacket_encrypted_tag && strcmp(e.channel_id, "PKI") == 0) {
        const meshtastic_NodeInfoLite *tx = nodeDB->getMeshNode(getFrom(p.get()));
        const meshtastic_NodeInfoLite *rx = nodeDB->getMeshNode(p->to);
        // Only accept PKI messages to us, or if we have both the sender and receiver in our nodeDB, as then it's
        // likely they discovered each other via a channel we have downlink enabled for
        if (isToUs(p.get()) || (tx && tx->has_user && rx && rx->has_user))
            router->enqueueReceivedMessage(p.release());
    } else if (router &&
               perhapsDecode(p.get()) == DecodeState::DECODE_SUCCESS) // ignore messages if we don't have the channel key
        router->enqueueReceivedMessage(p.release());
}

#if !defined(ARCH_NRF52) || NRF52_USE_JSON
// returns true if this is a valid JSON envelope which we accept on downlink
inline bool isValidJsonEnvelope(JSONObject &json)
{
    // if "sender" is provided, avoid processing packets we uplinked
    return (json.find("sender") != json.end() ? (json["sender"]->AsString().compare(owner.id) != 0) : true) &&
           (json.find("hopLimit") != json.end() ? json["hopLimit"]->IsNumber() : true) && // hop limit should be a number
           (json.find("from") != json.end()) && json["from"]->IsNumber() &&
           (json["from"]->AsNumber() == nodeDB->getNodeNum()) &&            // only accept message if the "from" is us
           (json.find("type") != json.end()) && json["type"]->IsString() && // should specify a type
           (json.find("payload") != json.end());                            // should have a payload
}

inline void onReceiveJson(byte *payload, size_t length)
{
    char payloadStr[length + 1];
    memcpy(payloadStr, payload, length);
    payloadStr[length] = 0; // null terminated string
    std::unique_ptr<JSONValue> json_value(JSON::Parse(payloadStr));
    if (json_value == nullptr) {
        LOG_ERROR("JSON received payload on MQTT but not a valid JSON");
        return;
    }

    JSONObject json;
    json = json_value->AsObject();

    if (!isValidJsonEnvelope(json)) {
        LOG_ERROR("JSON received payload on MQTT but not a valid envelope");
        return;
    }

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
            LOG_WARN("Received MQTT json payload too long, drop");
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
            pb_encode_to_bytes(p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes), &meshtastic_Position_msg,
                               &pos); // make the Data protobuf from position
        service->sendToMesh(p, RX_SRC_LOCAL);
    } else {
        LOG_DEBUG("JSON ignore downlink message with unsupported type");
    }
}
#endif

/// Determines if the given IPAddress is a private IPv4 address, i.e. not routable on the public internet.
bool isPrivateIpAddress(const IPAddress &ip)
{
    constexpr struct {
        uint32_t network;
        uint32_t mask;
    } privateCidrRanges[] = {
        {.network = 192u << 24 | 168 << 16, .mask = 0xffff0000}, // 192.168.0.0/16
        {.network = 172u << 24 | 16 << 16, .mask = 0xfff00000},  // 172.16.0.0/12
        {.network = 169u << 24 | 254 << 16, .mask = 0xffff0000}, // 169.254.0.0/16
        {.network = 10u << 24, .mask = 0xff000000},              // 10.0.0.0/8
        {.network = 127u << 24 | 1, .mask = 0xffffffff},         // 127.0.0.1/32
        {.network = 100u << 24 | 64 << 16, .mask = 0xffc00000},  // 100.64.0.0/10
    };
    const uint32_t addr = ntohl(ip);
    for (const auto &cidrRange : privateCidrRanges) {
        if (cidrRange.network == (addr & cidrRange.mask)) {
            LOG_INFO("MQTT server on a private IP");
            return true;
        }
    }
    return false;
}

// Separate a <host>[:<port>] string. Returns a pair containing the parsed host and port. If the port is
// not present in the input string, or is invalid, the value of the `port` argument will be returned.
std::pair<String, uint16_t> parseHostAndPort(String server, uint16_t port = 0)
{
    const int delimIndex = server.indexOf(':');
    if (delimIndex > 0) {
        const long parsedPort = server.substring(delimIndex + 1, server.length()).toInt();
        if (parsedPort < 1 || parsedPort > UINT16_MAX) {
            LOG_WARN("Invalid MQTT port %d: %s", parsedPort, server.c_str());
        } else {
            port = parsedPort;
        }
        server[delimIndex] = 0;
    }
    return std::make_pair(std::move(server), port);
}

bool isDefaultServer(const String &host)
{
    return host.length() == 0 || host == default_mqtt_address;
}

bool isDefaultRootTopic(const String &root)
{
    return root.length() == 0 || root == default_mqtt_root;
}

struct PubSubConfig {
    explicit PubSubConfig(const meshtastic_ModuleConfig_MQTTConfig &config)
    {
        if (*config.address) {
            serverAddr = config.address;
            mqttUsername = config.username;
            mqttPassword = config.password;
        }
        if (config.tls_enabled) {
            serverPort = 8883;
        }
        std::tie(serverAddr, serverPort) = parseHostAndPort(serverAddr.c_str(), serverPort);
    }

    // Defaults
    static constexpr uint16_t defaultPort = 1883;
    uint16_t serverPort = defaultPort;
    String serverAddr = default_mqtt_address;
    const char *mqttUsername = default_mqtt_username;
    const char *mqttPassword = default_mqtt_password;
};

#if HAS_NETWORKING
bool connectPubSub(const PubSubConfig &config, PubSubClient &pubSub, Client &client)
{
    pubSub.setBufferSize(1024, 1024);
    pubSub.setClient(client);
    pubSub.setServer(config.serverAddr.c_str(), config.serverPort);

    LOG_INFO("Connecting directly to MQTT server %s, port: %d, username: %s, password: %s", config.serverAddr.c_str(),
             config.serverPort, config.mqttUsername, config.mqttPassword);

    const bool connected = pubSub.connect(owner.id, config.mqttUsername, config.mqttPassword);
    if (connected) {
        LOG_INFO("MQTT connected");
    } else {
        LOG_WARN("Failed to connect to MQTT server");
    }
    return connected;
}
#endif

inline bool isConnectedToNetwork()
{
#ifdef USE_WS5500
    if (ETH.connected())
        return true;
#endif

#if HAS_WIFI
    return WiFi.isConnected();
#elif HAS_ETHERNET
    return Ethernet.linkStatus() == LinkON;
#else
    return false;
#endif
}

/** return true if we have a channel that wants uplink/downlink or map reporting is enabled
 */
bool wantsLink()
{
    const bool hasChannelorMapReport =
        moduleConfig.mqtt.enabled && (moduleConfig.mqtt.map_reporting_enabled || channels.anyMqttEnabled());
    return hasChannelorMapReport && (moduleConfig.mqtt.proxy_to_client_enabled || isConnectedToNetwork());
}
} // namespace

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
    if (length == 0) {
        LOG_WARN("Empty MQTT payload received, topic %s!", topic);
        return;
    }

    // check if this is a json payload message by comparing the topic start
    if (moduleConfig.mqtt.json_enabled && (strncmp(topic, jsonTopic.c_str(), jsonTopic.length()) == 0)) {
#if !defined(ARCH_NRF52) || NRF52_USE_JSON
        // parse the channel name from the topic string
        // the topic has been checked above for having jsonTopic prefix, so just move past it
        char *channelName = topic + jsonTopic.length();
        // if another "/" was added, parse string up to that character
        channelName = strtok(channelName, "/") ? strtok(channelName, "/") : channelName;
        // We allow downlink JSON packets only on a channel named "mqtt"
        const meshtastic_Channel &sendChannel = channels.getByName(channelName);
        if (!(strncasecmp(channels.getGlobalId(sendChannel.index), Channels::mqttChannel, strlen(Channels::mqttChannel)) == 0 &&
              sendChannel.settings.downlink_enabled)) {
            LOG_WARN("JSON downlink received on channel not called 'mqtt' or without downlink enabled");
            return;
        }
        onReceiveJson(payload, length);
#endif
        return;
    }

    onReceiveProto(topic, payload, length);
}

void mqttInit()
{
    new MQTT();
}

#if HAS_NETWORKING
MQTT::MQTT() : MQTT(std::unique_ptr<MQTTClient>(new MQTTClient())) {}
MQTT::MQTT(std::unique_ptr<MQTTClient> _mqttClient)
    : concurrency::OSThread("mqtt"), mqttQueue(MAX_MQTT_QUEUE), mqttClient(std::move(_mqttClient)), pubSub(*mqttClient)
#else
MQTT::MQTT() : concurrency::OSThread("mqtt"), mqttQueue(MAX_MQTT_QUEUE)
#endif
{
    if (moduleConfig.mqtt.enabled) {
        LOG_DEBUG("Init MQTT");

        assert(!mqtt);
        mqtt = this;

        if (*moduleConfig.mqtt.root) {
            cryptTopic = moduleConfig.mqtt.root + cryptTopic;
            jsonTopic = moduleConfig.mqtt.root + jsonTopic;
            mapTopic = moduleConfig.mqtt.root + mapTopic;
            isConfiguredForDefaultRootTopic = isDefaultRootTopic(moduleConfig.mqtt.root);
        } else {
            cryptTopic = "msh" + cryptTopic;
            jsonTopic = "msh" + jsonTopic;
            mapTopic = "msh" + mapTopic;
            isConfiguredForDefaultRootTopic = true;
        }

        if (moduleConfig.mqtt.map_reporting_enabled && moduleConfig.mqtt.has_map_report_settings) {
            map_position_precision = Default::getConfiguredOrDefault(moduleConfig.mqtt.map_report_settings.position_precision,
                                                                     default_map_position_precision);
            map_publish_interval_msecs = Default::getConfiguredOrDefaultMs(
                moduleConfig.mqtt.map_report_settings.publish_interval_secs, default_map_publish_interval_secs);
        }

        String host = parseHostAndPort(moduleConfig.mqtt.address).first;
        isConfiguredForDefaultServer = isDefaultServer(host);
        IPAddress ip;
        isMqttServerAddressPrivate = ip.fromString(host.c_str()) && isPrivateIpAddress(ip);

#if HAS_NETWORKING
        if (!moduleConfig.mqtt.proxy_to_client_enabled)
            pubSub.setCallback(mqttCallback);
#endif

        if (moduleConfig.mqtt.proxy_to_client_enabled) {
            LOG_INFO("MQTT configured to use client proxy");
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
            LOG_INFO("MQTT connect via client proxy instead");
            enabled = true;
            runASAP = true;
            reconnectCount = 0;

            publishNodeInfo();
            return; // Don't try to connect directly to the server
        }
#if HAS_NETWORKING
        const PubSubConfig ps_config(moduleConfig.mqtt);
        MQTTClient *clientConnection = mqttClient.get();
#if MQTT_SUPPORTS_TLS
        if (moduleConfig.mqtt.tls_enabled) {
            mqttClientTLS.setInsecure();
            LOG_INFO("Use TLS-encrypted session");
            clientConnection = &mqttClientTLS;
        } else {
            LOG_INFO("Use non-TLS-encrypted session");
        }
#endif
        if (connectPubSub(ps_config, pubSub, *clientConnection)) {
            enabled = true; // Start running background process again
            runASAP = true;
            reconnectCount = 0;
            isMqttServerAddressPrivate = isPrivateIpAddress(clientConnection->remoteIP());

            publishNodeInfo();
            sendSubscriptions();
        } else {
#if HAS_WIFI && !defined(ARCH_PORTDUINO)
            reconnectCount++;
            LOG_ERROR("Failed to contact MQTT server directly (%d/%d)", reconnectCount, reconnectMax);
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
            LOG_INFO("Subscribe to %s", topic.c_str());
            pubSub.subscribe(topic.c_str(), 1); // FIXME, is QOS 1 right?
#if !defined(ARCH_NRF52) ||                                                                                                      \
    defined(NRF52_USE_JSON) // JSON is not supported on nRF52, see issue #2804 ### Fixed by using ArduinoJSON ###
            if (moduleConfig.mqtt.json_enabled == true) {
                std::string topicDecoded = jsonTopic + channels.getGlobalId(i) + "/+";
                LOG_INFO("Subscribe to %s", topicDecoded.c_str());
                pubSub.subscribe(topicDecoded.c_str(), 1); // FIXME, is QOS 1 right?
            }
#endif // ARCH_NRF52 NRF52_USE_JSON
        }
    }
#if !MESHTASTIC_EXCLUDE_PKI
    if (hasDownlink) {
        std::string topic = cryptTopic + "PKI/+";
        LOG_INFO("Subscribe to %s", topic.c_str());
        pubSub.subscribe(topic.c_str(), 1);
    }
#endif
#endif
}

int32_t MQTT::runOnce()
{
    if (!moduleConfig.mqtt.enabled || !(moduleConfig.mqtt.map_reporting_enabled || channels.anyMqttEnabled()))
        return disable();
    bool wantConnection = wantsLink();

    perhapsReportToMap();

    // If connected poll rapidly, otherwise only occasionally check for a wifi connection change and ability to contact server
    if (moduleConfig.mqtt.proxy_to_client_enabled) {
        publishQueuedMessages();
        return 200;
    }
#if HAS_NETWORKING
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
            LOG_INFO("MQTT link not needed, drop");
            pubSub.disconnect();
        }

        powerFSM.trigger(EVENT_CONTACT_FROM_PHONE); // Suppress entering light sleep (because that would turn off bluetooth)
        return 20;
    }
#else
    // No networking available, return default interval
    return 30000;
#endif
}

bool MQTT::isValidConfig(const meshtastic_ModuleConfig_MQTTConfig &config, MQTTClient *client)
{
    const PubSubConfig parsed(config);

    if (config.enabled && !config.proxy_to_client_enabled) {
#if HAS_NETWORKING
        std::unique_ptr<MQTTClient> clientConnection;
        if (config.tls_enabled) {
#if MQTT_SUPPORTS_TLS
            MQTTClientTLS *tlsClient = new MQTTClientTLS;
            clientConnection.reset(tlsClient);
            tlsClient->setInsecure();
#else
            LOG_ERROR("Invalid MQTT config: tls_enabled is not supported on this node");
            return false;
#endif
        } else {
            clientConnection.reset(new MQTTClient);
        }
        std::unique_ptr<PubSubClient> pubSub(new PubSubClient);
        if (isConnectedToNetwork()) {
            return connectPubSub(parsed, *pubSub, (client != nullptr) ? *client : *clientConnection);
        }
#else
        const char *warning = "Invalid MQTT config: proxy_to_client_enabled must be enabled on nodes that do not have a network";
        LOG_ERROR(warning);
#if !IS_RUNNING_TESTS
        meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
        cn->level = meshtastic_LogRecord_Level_ERROR;
        cn->time = getValidTime(RTCQualityFromNet);
        strncpy(cn->message, warning, sizeof(cn->message) - 1);
        cn->message[sizeof(cn->message) - 1] = '\0'; // Ensure null termination
        service->sendClientNotification(cn);
#endif
        return false;
#endif
    }

    const bool defaultServer = isDefaultServer(parsed.serverAddr);
    if (defaultServer && parsed.serverPort != PubSubConfig::defaultPort) {
        const char *warning = "Invalid MQTT config: default server address must not have a port specified";
        LOG_ERROR(warning);
#if !IS_RUNNING_TESTS
        meshtastic_ClientNotification *cn = clientNotificationPool.allocZeroed();
        cn->level = meshtastic_LogRecord_Level_ERROR;
        cn->time = getValidTime(RTCQualityFromNet);
        strncpy(cn->message, warning, sizeof(cn->message) - 1);
        cn->message[sizeof(cn->message) - 1] = '\0'; // Ensure null termination
        service->sendClientNotification(cn);
#endif
        return false;
    }
    return true;
}

void MQTT::publishNodeInfo()
{
    // TODO: NodeInfo broadcast over MQTT only (NODENUM_BROADCAST_NO_LORA)
}
void MQTT::publishQueuedMessages()
{
    if (mqttQueue.isEmpty())
        return;

    LOG_DEBUG("Publish enqueued MQTT message");
    const std::unique_ptr<QueueEntry> entry(mqttQueue.dequeuePtr(0));
    LOG_INFO("publish %s, %u bytes from queue", entry->topic.c_str(), entry->envBytes.size());
    publish(entry->topic.c_str(), entry->envBytes.data(), entry->envBytes.size(), false);

#if !defined(ARCH_NRF52) ||                                                                                                      \
    defined(NRF52_USE_JSON) // JSON is not supported on nRF52, see issue #2804 ### Fixed by using ArduinoJson ###
    if (!moduleConfig.mqtt.json_enabled)
        return;

    // handle json topic
    const DecodedServiceEnvelope env(entry->envBytes.data(), entry->envBytes.size());
    if (!env.validDecode || env.packet == NULL || env.channel_id == NULL)
        return;

    auto jsonString = MeshPacketSerializer::JsonSerialize(env.packet);
    if (jsonString.length() == 0)
        return;

    std::string topicJson;
    if (env.packet->pki_encrypted) {
        topicJson = jsonTopic + "PKI/" + owner.id;
    } else {
        topicJson = jsonTopic + env.channel_id + "/" + owner.id;
    }
    LOG_INFO("JSON publish message to %s, %u bytes: %s", topicJson.c_str(), jsonString.length(), jsonString.c_str());
    publish(topicJson.c_str(), jsonString.c_str(), false);
#endif // ARCH_NRF52 NRF52_USE_JSON
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
        // For uplinking other's packets, check if it's not OK to MQTT or if it's an older packet without the bitfield
        bool dontUplink = !mp_decoded.decoded.has_bitfield || !(mp_decoded.decoded.bitfield & BITFIELD_OK_TO_MQTT_MASK);
        // check for the lowest bit of the data bitfield set false, and the use of one of the default keys.
        if (!isFromUs(&mp_decoded) && !isMqttServerAddressPrivate && dontUplink &&
            (ch.settings.psk.size < 2 || (ch.settings.psk.size == 16 && memcmp(ch.settings.psk.bytes, defaultpsk, 16)) ||
             (ch.settings.psk.size == 32 && memcmp(ch.settings.psk.bytes, eventpsk, 32)))) {
            LOG_INFO("MQTT onSend - Not forwarding packet due to DontMqttMeBro flag");
            return;
        }

        if (isConfiguredForDefaultServer && (mp_decoded.decoded.portnum == meshtastic_PortNum_RANGE_TEST_APP ||
                                             mp_decoded.decoded.portnum == meshtastic_PortNum_DETECTION_SENSOR_APP)) {
            LOG_DEBUG("MQTT onSend - Ignoring range test or detection sensor message on public mqtt");
            return;
        }
    }
    // Either encrypted packet (we couldn't decrypt) is marked as pki_encrypted, or we could decode the PKI encrypted packet
    bool isPKIEncrypted = mp_encrypted.pki_encrypted || mp_decoded.pki_encrypted;
    // If it was to a channel, check uplink enabled, else must be pki_encrypted
    if (!(ch.settings.uplink_enabled || isPKIEncrypted))
        return;
    const char *channelId = isPKIEncrypted ? "PKI" : channels.getGlobalId(chIndex);

    LOG_DEBUG("MQTT onSend - Publish ");
    const meshtastic_MeshPacket *p;
    if (moduleConfig.mqtt.encryption_enabled) {
        p = &mp_encrypted;
        LOG_DEBUG("encrypted message");
    } else if (mp_decoded.which_payload_variant == meshtastic_MeshPacket_decoded_tag) {
        p = &mp_decoded;
        LOG_DEBUG("portnum %i message", mp_decoded.decoded.portnum);
    } else {
        LOG_DEBUG("nothing, pkt not decrypted");
        return; // Don't upload a still-encrypted PKI packet if not encryption_enabled
    }

    const meshtastic_ServiceEnvelope env = {
        .packet = const_cast<meshtastic_MeshPacket *>(p), .channel_id = const_cast<char *>(channelId), .gateway_id = owner.id};
    size_t numBytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_ServiceEnvelope_msg, &env);
    std::string topic = cryptTopic + channelId + "/" + owner.id;

    if (moduleConfig.mqtt.proxy_to_client_enabled || this->isConnectedDirectly()) {
        LOG_DEBUG("MQTT Publish %s, %u bytes", topic.c_str(), numBytes);
        publish(topic.c_str(), bytes, numBytes, false);

#if !defined(ARCH_NRF52) ||                                                                                                      \
    defined(NRF52_USE_JSON) // JSON is not supported on nRF52, see issue #2804 ### Fixed by using ArduinoJson ###
        if (!moduleConfig.mqtt.json_enabled)
            return;
        // handle json topic
        auto jsonString = MeshPacketSerializer::JsonSerialize(&mp_decoded);
        if (jsonString.length() == 0)
            return;
        std::string topicJson = jsonTopic + channelId + "/" + owner.id;
        LOG_INFO("JSON publish message to %s, %u bytes: %s", topicJson.c_str(), jsonString.length(), jsonString.c_str());
        publish(topicJson.c_str(), jsonString.c_str(), false);
#endif // ARCH_NRF52 NRF52_USE_JSON
    } else {
        LOG_INFO("MQTT not connected, queue packet");
        QueueEntry *entry;
        if (mqttQueue.numFree() == 0) {
            LOG_WARN("MQTT queue is full, discard oldest");
            entry = mqttQueue.dequeuePtr(0);
        } else {
            entry = new QueueEntry;
        }
        entry->topic = std::move(topic);
        entry->envBytes.assign(bytes, numBytes);
        if (mqttQueue.enqueue(entry, 0) == false) {
            LOG_CRIT("Failed to add a message to mqttQueue!");
            abort();
        }
    }
}

void MQTT::perhapsReportToMap()
{
    if (!moduleConfig.mqtt.map_reporting_enabled || !moduleConfig.mqtt.map_report_settings.should_report_location ||
        !(moduleConfig.mqtt.proxy_to_client_enabled || isConnectedDirectly()))
        return;

    // Coerce the map position precision to be within the valid range
    // This removes obtusely large radius and privacy problematic ones from the map
    if (map_position_precision < 12 || map_position_precision > 15) {
        LOG_WARN("MQTT Map report position precision %u is out of range, using default %u", map_position_precision,
                 default_map_position_precision);
        map_position_precision = default_map_position_precision;
    }

    if (Throttle::isWithinTimespanMs(last_report_to_map, map_publish_interval_msecs))
        return;

    if (localPosition.latitude_i == 0 && localPosition.longitude_i == 0) {
        last_report_to_map = millis();
        LOG_WARN("MQTT Map report enabled, but no position available");
        return;
    }

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
    mapReport.has_opted_report_location = true;

    // Set position with precision (same as in PositionModule)
    mapReport.latitude_i = localPosition.latitude_i & (UINT32_MAX << (32 - map_position_precision));
    mapReport.longitude_i = localPosition.longitude_i & (UINT32_MAX << (32 - map_position_precision));
    mapReport.latitude_i += (1 << (31 - map_position_precision));
    mapReport.longitude_i += (1 << (31 - map_position_precision));

    mapReport.altitude = localPosition.altitude;
    mapReport.position_precision = map_position_precision;

    mapReport.num_online_local_nodes = nodeDB->getNumOnlineMeshNodes(true);

    // Encode MapReport message into the MeshPacket
    mp->decoded.payload.size =
        pb_encode_to_bytes(mp->decoded.payload.bytes, sizeof(mp->decoded.payload.bytes), &meshtastic_MapReport_msg, &mapReport);

    // Encode the MeshPacket into a binary ServiceEnvelope and publish
    const meshtastic_ServiceEnvelope se = {
        .packet = mp,
        .channel_id = (char *)channels.getGlobalId(channels.getPrimaryIndex()), // Use primary channel as the channel_id
        .gateway_id = owner.id};
    size_t numBytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_ServiceEnvelope_msg, &se);

    LOG_INFO("MQTT Publish map report to %s", mapTopic.c_str());
    publish(mapTopic.c_str(), bytes, numBytes, false);

    // Release the allocated memory for MeshPacket
    packetPool.release(mp);

    // Update the last report time
    last_report_to_map = millis();
}