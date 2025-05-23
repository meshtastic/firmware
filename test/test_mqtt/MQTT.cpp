#include "DebugConfiguration.h"
#include "TestUtil.h"
#include <unity.h>

#ifdef ARCH_PORTDUINO
#include "mesh/CryptoEngine.h"
#include "mesh/Default.h"
#include "mesh/MeshService.h"
#include "mesh/NodeDB.h"
#include "mesh/Router.h"
#include "modules/RoutingModule.h"
#include "mqtt/MQTT.h"
#include "mqtt/ServiceEnvelope.h"

#include <PubSubClient.h>
#include <WiFiClient.h>

#include <arpa/inet.h>

#include <algorithm>
#include <list>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace
{
// Minimal router needed to receive messages from MQTT.
class MockRouter : public Router
{
  public:
    ~MockRouter()
    {
        // cryptLock is created in the constructor for Router.
        delete cryptLock;
        cryptLock = NULL;
    }
    void enqueueReceivedMessage(meshtastic_MeshPacket *p) override
    {
        packets_.emplace_back(*p);
        packetPool.release(p);
    }
    std::list<meshtastic_MeshPacket> packets_; // Packets received by the Router.
};

// Minimal MeshService needed to receive messages from MQTT for testing PKI channel.
class MockMeshService : public MeshService
{
  public:
    void sendMqttMessageToClientProxy(meshtastic_MqttClientProxyMessage *m) override
    {
        messages_.emplace_back(*m);
        releaseMqttClientProxyMessageToPool(m);
    }
    std::list<meshtastic_MqttClientProxyMessage> messages_; // Messages received from the MeshService.
};

// Minimal NodeDB needed to return values from getMeshNode.
class MockNodeDB : public NodeDB
{
  public:
    meshtastic_NodeInfoLite *getMeshNode(NodeNum n) override { return &emptyNode; }
    meshtastic_NodeInfoLite emptyNode = {};
};

// Minimal RoutingModule needed to return values from sendAckNak.
class MockRoutingModule : public RoutingModule
{
  public:
    void sendAckNak(meshtastic_Routing_Error err, NodeNum to, PacketId idFrom, ChannelIndex chIndex,
                    uint8_t hopLimit = 0) override
    {
        ackNacks_.emplace_back(err, to, idFrom, chIndex, hopLimit);
    }
    std::list<std::tuple<meshtastic_Routing_Error, NodeNum, PacketId, ChannelIndex, uint8_t>>
        ackNacks_; // ackNacks received by the RoutingModule.
};

// A WiFi client used by the MQTT::PubSubClient. Implements a minimal pub/sub server.
// There isn't an easy way to mock PubSubClient due to it not having virtual methods, so we mock using
// the WiFiClinet that PubSubClient uses.
class MockPubSubServer : public WiFiClient
{
  public:
    static constexpr char kTextTopic[] = "TextTopic";
    uint8_t connected() override { return connected_; }
    void flush() override {}
    IPAddress remoteIP() const override { return IPAddress(htonl(ipAddress_)); }
    void stop() override { connected_ = false; }

    int connect(IPAddress ip, uint16_t port) override
    {
        port_ = port;
        if (refuseConnection_)
            return 0;
        connected_ = true;
        return 1;
    }
    int connect(const char *host, uint16_t port) override
    {
        host_ = host;
        port_ = port;
        if (refuseConnection_)
            return 0;
        connected_ = true;
        return 1;
    }

    int available() override
    {
        if (buffer_.empty())
            return 0;
        return buffer_.front().size();
    }

    int read() override
    {
        assert(available());
        std::string &front = buffer_.front();
        char ch = front[0];
        front = front.substr(1, front.size());
        if (front.empty())
            buffer_.pop_front();
        return ch;
    }

    size_t write(uint8_t data) override { return write(&data, 1); }
    size_t write(const uint8_t *buf, size_t size) override
    {
        command_ += std::string(reinterpret_cast<const char *>(buf), size);
        if (command_.size() < 2)
            return size;
        const int len = (uint8_t)command_[1] + 2;
        if (command_.size() < len)
            return size;
        handleCommand(command_[0], command_.substr(2, len));
        command_ = command_.substr(len, command_.size());
        return size;
    }

    // The pub/sub "server".
    // https://public.dhe.ibm.com/software/dw/webservices/ws-mqtt/MQTT_V3.1_Protocol_Specific.pdf
    void handleCommand(uint8_t header, std::string_view message)
    {
        switch (header & 0xf0) {
        case MQTTCONNECT:
            LOG_DEBUG("MQTTCONNECT");
            buffer_.push_back(std::string("\x20\x02\x00\x00", 4));
            break;

        case MQTTSUBSCRIBE: {
            LOG_DEBUG("MQTTSUBSCRIBE");
            assert(message.size() >= 5);
            message.remove_prefix(2); // skip messageId

            while (message.size() >= 3) {
                const uint16_t topicSize = ((uint8_t)message[0]) << 8 | (uint8_t)message[1];
                message.remove_prefix(2);

                assert(message.size() >= topicSize + 1);
                std::string topic(message.data(), topicSize);
                message.remove_prefix(topicSize + 1);

                LOG_DEBUG("Subscribed to topic: %s", topic.c_str());
                subscriptions_.insert(std::move(topic));
            }
            break;
        }

        case MQTTPINGREQ:
            LOG_DEBUG("MQTTPINGREQ");
            buffer_.push_back(std::string("\xd0\x00", 2));
            break;

        case MQTTPUBLISH: {
            LOG_DEBUG("MQTTPUBLISH");
            assert(message.size() >= 3);
            const uint16_t topicSize = ((uint8_t)message[0]) << 8 | (uint8_t)message[1];
            message.remove_prefix(2);

            assert(message.size() >= topicSize);
            std::string topic(message.data(), topicSize);
            message.remove_prefix(topicSize);

            if (topic == kTextTopic) {
                published_.emplace_back(std::move(topic), std::string(message.data(), message.size()));
            } else {
                published_.emplace_back(
                    std::move(topic), DecodedServiceEnvelope(reinterpret_cast<const uint8_t *>(message.data()), message.size()));
            }
            break;
        }
        }
    }

    bool connected_ = false;
    bool refuseConnection_ = false;       // Simulate a failed connection.
    uint32_t ipAddress_ = 0x01010101;     // IP address of the MQTT server.
    std::string host_;                    // Requested host.
    uint16_t port_;                       // Requested port.
    std::list<std::string> buffer_;       // Buffer of messages for the pubSub client to receive.
    std::string command_;                 // Current command received from the pubSub client.
    std::set<std::string> subscriptions_; // Topics that the pubSub client has subscribed to.
    std::list<std::pair<std::string, std::variant<std::string,
                                                  DecodedServiceEnvelope>>>
        published_; // Messages published from the pubSub client. Each list element is a pair containing the topic name and either
                    // a text message (if from the kTextTopic topic) or a DecodedServiceEnvelope.
};

// Instances of our mocks.
class MQTTUnitTest;
MQTTUnitTest *unitTest;
MockPubSubServer *pubsub;
MockRoutingModule *mockRoutingModule;
MockMeshService *mockMeshService;
MockRouter *mockRouter;

// Keep running the loop until either conditionMet returns true or 4 seconds elapse.
// Returns true if conditionMet returns true, returns false on timeout.
bool loopUntil(std::function<bool()> conditionMet)
{
    long start = millis();
    while (start + 4000 > millis()) {
        long delayMsec = concurrency::mainController.runOrDelay();
        if (conditionMet())
            return true;
        concurrency::mainDelay.delay(std::min(delayMsec, 5L));
    }
    return false;
}

// Used to access protected/private members of MQTT for unit testing.
class MQTTUnitTest : public MQTT
{
  public:
    MQTTUnitTest() : MQTT(std::make_unique<MockPubSubServer>())
    {
        pubsub = reinterpret_cast<MockPubSubServer *>(mqttClient.get());
    }
    ~MQTTUnitTest()
    {
        // Needed because WiFiClient does not have a virtual destructor.
        mqttClient.release();
        delete pubsub;
    }
    using MQTT::isValidConfig;
    using MQTT::reconnect;
    int queueSize() { return mqttQueue.numUsed(); }
    void reportToMap(std::optional<uint32_t> precision = std::nullopt)
    {
        if (precision.has_value())
            map_position_precision = precision.value();
        map_publish_interval_msecs = 0;
        perhapsReportToMap();
    }
    void publish(const meshtastic_MeshPacket *p, std::string gateway = "!87654321", std::string channel = "test")
    {
        std::stringstream topic;
        topic << "msh/2/e/" << channel << "/!" << gateway;
        const meshtastic_ServiceEnvelope env = {.packet = const_cast<meshtastic_MeshPacket *>(p),
                                                .channel_id = const_cast<char *>(channel.c_str()),
                                                .gateway_id = const_cast<char *>(gateway.c_str())};
        uint8_t bytes[256];
        size_t numBytes = pb_encode_to_bytes(bytes, sizeof(bytes), &meshtastic_ServiceEnvelope_msg, &env);
        mqttCallback(const_cast<char *>(topic.str().c_str()), bytes, numBytes);
    }
    static void restart()
    {
        if (mqtt != NULL) {
            delete mqtt;
            mqtt = unitTest = NULL;
        }
        mqtt = unitTest = new MQTTUnitTest();
        mqtt->start();

        if (!moduleConfig.mqtt.enabled || moduleConfig.mqtt.proxy_to_client_enabled || *moduleConfig.mqtt.root) {
            loopUntil([] { return true; }); // Loop once
            return;
        }
        // Wait for MQTT to subscribe to all topics.
        TEST_ASSERT_TRUE(loopUntil(
            [] { return pubsub->subscriptions_.count("msh/2/e/test/+") && pubsub->subscriptions_.count("msh/2/e/PKI/+"); }));
    }
    PubSubClient &getPubSub() { return pubSub; }
};

// Packets used in unit tests.
const meshtastic_MeshPacket decoded = {
    .from = 1,
    .to = 2,
    .which_payload_variant = meshtastic_MeshPacket_decoded_tag,
    .decoded = {.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP, .has_bitfield = true, .bitfield = BITFIELD_OK_TO_MQTT_MASK},
    .id = 4,
};
const meshtastic_MeshPacket encrypted = {
    .from = 1,
    .to = 2,
    .which_payload_variant = meshtastic_MeshPacket_encrypted_tag,
    .encrypted = {.size = 0},
    .id = 3,
};
} // namespace

// Initialize mocks and configuration before running each test.
void setUp(void)
{
    moduleConfig.mqtt =
        meshtastic_ModuleConfig_MQTTConfig{.enabled = true, .map_reporting_enabled = true, .has_map_report_settings = true};
    moduleConfig.mqtt.map_report_settings = meshtastic_ModuleConfig_MapReportSettings{
        .publish_interval_secs = 0, .position_precision = 14, .should_report_location = true};
    channelFile.channels[0] = meshtastic_Channel{
        .index = 0,
        .has_settings = true,
        .settings = {.name = "test", .uplink_enabled = true, .downlink_enabled = true},
        .role = meshtastic_Channel_Role_PRIMARY,
    };
    channelFile.channels_count = 1;
    owner = meshtastic_User{.id = "!12345678"};
    myNodeInfo = meshtastic_MyNodeInfo{.my_node_num = 10};
    localPosition =
        meshtastic_Position{.has_latitude_i = true, .latitude_i = 7 * 1e7, .has_longitude_i = true, .longitude_i = 3 * 1e7};

    router = mockRouter = new MockRouter();
    service = mockMeshService = new MockMeshService();
    routingModule = mockRoutingModule = new MockRoutingModule();
    MQTTUnitTest::restart();
}

// Deinitialize all objects created in setUp.
void tearDown(void)
{
    delete unitTest;
    mqtt = unitTest = NULL;
    delete mockRoutingModule;
    routingModule = mockRoutingModule = NULL;
    delete mockMeshService;
    service = mockMeshService = NULL;
    delete mockRouter;
    router = mockRouter = NULL;
}

// Test that the decoded MeshPacket is published when encryption_enabled = false.
void test_sendDirectlyConnectedDecoded(void)
{
    mqtt->onSend(encrypted, decoded, 0);

    TEST_ASSERT_EQUAL(1, pubsub->published_.size());
    const auto &[topic, payload] = pubsub->published_.front();
    const DecodedServiceEnvelope &env = std::get<DecodedServiceEnvelope>(payload);
    TEST_ASSERT_EQUAL_STRING("msh/2/e/test/!12345678", topic.c_str());
    TEST_ASSERT_TRUE(env.validDecode);
    TEST_ASSERT_EQUAL(decoded.id, env.packet->id);
}

// Test that the encrypted MeshPacket is published when encryption_enabled = true.
void test_sendDirectlyConnectedEncrypted(void)
{
    moduleConfig.mqtt.encryption_enabled = true;

    mqtt->onSend(encrypted, decoded, 0);

    TEST_ASSERT_EQUAL(1, pubsub->published_.size());
    const auto &[topic, payload] = pubsub->published_.front();
    const DecodedServiceEnvelope &env = std::get<DecodedServiceEnvelope>(payload);
    TEST_ASSERT_EQUAL_STRING("msh/2/e/test/!12345678", topic.c_str());
    TEST_ASSERT_TRUE(env.validDecode);
    TEST_ASSERT_EQUAL(encrypted.id, env.packet->id);
}

// Verify that the decoded MeshPacket is proxied through the MeshService when encryption_enabled = false.
void test_proxyToMeshServiceDecoded(void)
{
    moduleConfig.mqtt.proxy_to_client_enabled = true;
    MQTTUnitTest::restart();

    mqtt->onSend(encrypted, decoded, 0);

    TEST_ASSERT_EQUAL(1, mockMeshService->messages_.size());
    const meshtastic_MqttClientProxyMessage &message = mockMeshService->messages_.front();
    TEST_ASSERT_EQUAL_STRING("msh/2/e/test/!12345678", message.topic);
    TEST_ASSERT_EQUAL(meshtastic_MqttClientProxyMessage_data_tag, message.which_payload_variant);
    const DecodedServiceEnvelope env(message.payload_variant.data.bytes, message.payload_variant.data.size);
    TEST_ASSERT_TRUE(env.validDecode);
    TEST_ASSERT_EQUAL(decoded.id, env.packet->id);
}

// Verify that the encrypted MeshPacket is proxied through the MeshService when encryption_enabled = true.
void test_proxyToMeshServiceEncrypted(void)
{
    moduleConfig.mqtt.proxy_to_client_enabled = true;
    moduleConfig.mqtt.encryption_enabled = true;
    MQTTUnitTest::restart();

    mqtt->onSend(encrypted, decoded, 0);

    TEST_ASSERT_EQUAL(1, mockMeshService->messages_.size());
    const meshtastic_MqttClientProxyMessage &message = mockMeshService->messages_.front();
    TEST_ASSERT_EQUAL_STRING("msh/2/e/test/!12345678", message.topic);
    TEST_ASSERT_EQUAL(meshtastic_MqttClientProxyMessage_data_tag, message.which_payload_variant);
    const DecodedServiceEnvelope env(message.payload_variant.data.bytes, message.payload_variant.data.size);
    TEST_ASSERT_TRUE(env.validDecode);
    TEST_ASSERT_EQUAL(encrypted.id, env.packet->id);
}

// A packet without the OK to MQTT bit set should not be published to a public server.
void test_dontMqttMeOnPublicServer(void)
{
    meshtastic_MeshPacket p = decoded;
    p.decoded.bitfield = 0;
    p.decoded.has_bitfield = 0;

    mqtt->onSend(encrypted, p, 0);

    TEST_ASSERT_TRUE(pubsub->published_.empty());
}

// A packet without the OK to MQTT bit set should be published to a private server.
void test_okToMqttOnPrivateServer(void)
{
    // Cause a disconnect.
    pubsub->connected_ = false;
    pubsub->refuseConnection_ = true;
    TEST_ASSERT_TRUE(loopUntil([] { return !unitTest->getPubSub().connected(); }));

    // Use 127.0.0.1 for the server's IP.
    pubsub->ipAddress_ = 0x7f000001;

    // Reconnect.
    pubsub->refuseConnection_ = false;
    TEST_ASSERT_TRUE(loopUntil([] { return unitTest->getPubSub().connected(); }));

    // Send the same packet as test_dontMqttMeOnPublicServer.
    meshtastic_MeshPacket p = decoded;
    p.decoded.bitfield = 0;
    p.decoded.has_bitfield = 0;

    mqtt->onSend(encrypted, p, 0);

    TEST_ASSERT_EQUAL(1, pubsub->published_.size());
}

// Range tests messages are not uplinked to the default server.
void test_noRangeTestAppOnDefaultServer(void)
{
    meshtastic_MeshPacket p = decoded;
    p.decoded.portnum = meshtastic_PortNum_RANGE_TEST_APP;

    mqtt->onSend(encrypted, p, 0);

    TEST_ASSERT_TRUE(pubsub->published_.empty());
}

// Detection sensor messages are not uplinked to the default server.
void test_noDetectionSensorAppOnDefaultServer(void)
{
    meshtastic_MeshPacket p = decoded;
    p.decoded.portnum = meshtastic_PortNum_DETECTION_SENSOR_APP;

    mqtt->onSend(encrypted, p, 0);

    TEST_ASSERT_TRUE(pubsub->published_.empty());
}

// Test that a MeshPacket is queued while the MQTT server is disconnected.
void test_sendQueued(void)
{
    // Cause a disconnect.
    pubsub->connected_ = false;
    pubsub->refuseConnection_ = true;
    TEST_ASSERT_TRUE(loopUntil([] { return !unitTest->getPubSub().connected(); }));

    // Send while disconnected.
    mqtt->onSend(encrypted, decoded, 0);
    TEST_ASSERT_EQUAL(1, unitTest->queueSize());
    TEST_ASSERT_TRUE(pubsub->published_.empty());
    TEST_ASSERT_FALSE(unitTest->getPubSub().connected());

    // Allow reconnect to happen. Expect to see the packet published now.
    pubsub->refuseConnection_ = false;
    TEST_ASSERT_TRUE(loopUntil([] { return !pubsub->published_.empty(); }));

    TEST_ASSERT_EQUAL(0, unitTest->queueSize());
    const auto &[topic, payload] = pubsub->published_.front();
    const DecodedServiceEnvelope &env = std::get<DecodedServiceEnvelope>(payload);
    TEST_ASSERT_EQUAL_STRING("msh/2/e/test/!12345678", topic.c_str());
    TEST_ASSERT_TRUE(env.validDecode);
    TEST_ASSERT_EQUAL(decoded.id, env.packet->id);
}

// Verify reconnecting with the proxy enabled does not reconnect to a MQTT server.
void test_reconnectProxyDoesNotReconnectMqtt(void)
{
    moduleConfig.mqtt.proxy_to_client_enabled = true;
    MQTTUnitTest::restart();

    unitTest->reconnect();

    TEST_ASSERT_FALSE(pubsub->connected_);
}

// Test receiving an empty MeshPacket on a subscribed topic.
void test_receiveEmptyMeshPacket(void)
{
    unitTest->publish(NULL);

    TEST_ASSERT_TRUE(mockRouter->packets_.empty());
    TEST_ASSERT_TRUE(mockRoutingModule->ackNacks_.empty());
}

// Test receiving a decoded MeshPacket on a subscribed topic.
void test_receiveDecodedProto(void)
{
    unitTest->publish(&decoded);

    TEST_ASSERT_EQUAL(1, mockRouter->packets_.size());
    const meshtastic_MeshPacket &p = mockRouter->packets_.front();
    TEST_ASSERT_EQUAL(decoded.id, p.id);
    TEST_ASSERT_TRUE(p.via_mqtt);
}

// Test receiving a decoded MeshPacket from the phone proxy.
void test_receiveDecodedProtoFromProxy(void)
{
    const meshtastic_ServiceEnvelope env = {
        .packet = const_cast<meshtastic_MeshPacket *>(&decoded), .channel_id = "test", .gateway_id = "!87654321"};
    meshtastic_MqttClientProxyMessage message = meshtastic_MqttClientProxyMessage_init_default;
    strcat(message.topic, "msh/2/e/test/!87654321");
    message.which_payload_variant = meshtastic_MqttClientProxyMessage_data_tag;
    message.payload_variant.data.size = pb_encode_to_bytes(
        message.payload_variant.data.bytes, sizeof(message.payload_variant.data.bytes), &meshtastic_ServiceEnvelope_msg, &env);

    mqtt->onClientProxyReceive(message);

    TEST_ASSERT_EQUAL(1, mockRouter->packets_.size());
    const meshtastic_MeshPacket &p = mockRouter->packets_.front();
    TEST_ASSERT_EQUAL(decoded.id, p.id);
    TEST_ASSERT_TRUE(p.via_mqtt);
}

// Properly handles the case where the received message is empty.
void test_receiveEmptyDataFromProxy(void)
{
    meshtastic_MqttClientProxyMessage message = meshtastic_MqttClientProxyMessage_init_default;
    message.which_payload_variant = meshtastic_MqttClientProxyMessage_data_tag;

    mqtt->onClientProxyReceive(message);

    TEST_ASSERT_TRUE(mockRouter->packets_.empty());
}

// Packets should be ignored if downlink is not enabled.
void test_receiveWithoutChannelDownlink(void)
{
    channelFile.channels[0].settings.downlink_enabled = false;

    unitTest->publish(&decoded);

    TEST_ASSERT_TRUE(mockRouter->packets_.empty());
}

// Test receiving an encrypted MeshPacket on the PKI topic.
void test_receiveEncryptedPKITopicToUs(void)
{
    meshtastic_MeshPacket e = encrypted;
    e.to = myNodeInfo.my_node_num;

    unitTest->publish(&e, "!87654321", "PKI");

    TEST_ASSERT_EQUAL(1, mockRouter->packets_.size());
    const meshtastic_MeshPacket &p = mockRouter->packets_.front();
    TEST_ASSERT_EQUAL(encrypted.id, p.id);
    TEST_ASSERT_TRUE(p.via_mqtt);
}

// Should ignore messages published to MQTT by this gateway.
void test_receiveIgnoresOwnPublishedMessages(void)
{
    unitTest->publish(&decoded, owner.id);

    TEST_ASSERT_TRUE(mockRouter->packets_.empty());
    TEST_ASSERT_TRUE(mockRoutingModule->ackNacks_.empty());
}

// Considers receiving one of our packets an acknowledgement of it being sent.
void test_receiveAcksOwnSentMessages(void)
{
    meshtastic_MeshPacket p = decoded;
    p.from = myNodeInfo.my_node_num;

    unitTest->publish(&p, owner.id);

    TEST_ASSERT_TRUE(mockRouter->packets_.empty());
    TEST_ASSERT_EQUAL(1, mockRoutingModule->ackNacks_.size());
    const auto &[err, to, idFrom, chIndex, hopLimit] = mockRoutingModule->ackNacks_.front();
    TEST_ASSERT_EQUAL(meshtastic_Routing_Error_NONE, err);
    TEST_ASSERT_EQUAL(myNodeInfo.my_node_num, to);
    TEST_ASSERT_EQUAL(p.id, idFrom);
}

// Should ignore our own messages from MQTT that were heard by other nodes.
void test_receiveIgnoresSentMessagesFromOthers(void)
{
    meshtastic_MeshPacket p = decoded;
    p.from = myNodeInfo.my_node_num;

    unitTest->publish(&p);

    TEST_ASSERT_TRUE(mockRouter->packets_.empty());
    TEST_ASSERT_TRUE(mockRoutingModule->ackNacks_.empty());
}

// Decoded MQTT messages should be ignored when encryption is enabled.
void test_receiveIgnoresDecodedWhenEncryptionEnabled(void)
{
    moduleConfig.mqtt.encryption_enabled = true;

    unitTest->publish(&decoded);

    TEST_ASSERT_TRUE(mockRouter->packets_.empty());
}

// Non-encrypted messages for the Admin App should be ignored.
void test_receiveIgnoresDecodedAdminApp(void)
{
    meshtastic_MeshPacket p = decoded;
    p.decoded.portnum = meshtastic_PortNum_ADMIN_APP;

    unitTest->publish(&p);

    TEST_ASSERT_TRUE(mockRouter->packets_.empty());
}

// Only the same fields that are transmitted over LoRa should be set in MQTT messages.
void test_receiveIgnoresUnexpectedFields(void)
{
    meshtastic_MeshPacket input = decoded;
    input.rx_snr = 10;
    input.rx_rssi = 20;

    unitTest->publish(&input);

    TEST_ASSERT_EQUAL(1, mockRouter->packets_.size());
    const meshtastic_MeshPacket &p = mockRouter->packets_.front();
    TEST_ASSERT_EQUAL(0, p.rx_snr);
    TEST_ASSERT_EQUAL(0, p.rx_rssi);
}

// Messages with an invalid hop_limit are ignored.
void test_receiveIgnoresInvalidHopLimit(void)
{
    meshtastic_MeshPacket p = decoded;
    p.hop_limit = 10;

    unitTest->publish(&p);

    TEST_ASSERT_TRUE(mockRouter->packets_.empty());
}

// Publishing to a text channel.
void test_publishTextMessageDirect(void)
{
    TEST_ASSERT_TRUE(mqtt->publish(MockPubSubServer::kTextTopic, "payload", 0));

    TEST_ASSERT_EQUAL(1, pubsub->published_.size());
    const auto &[topic, payload] = pubsub->published_.front();
    TEST_ASSERT_EQUAL_STRING("payload", std::get<std::string>(payload).c_str());
}

// Publishing to a text channel via the MQTT client proxy.
void test_publishTextMessageWithProxy(void)
{
    moduleConfig.mqtt.proxy_to_client_enabled = true;

    TEST_ASSERT_TRUE(mqtt->publish(MockPubSubServer::kTextTopic, "payload", 0));

    TEST_ASSERT_EQUAL(1, mockMeshService->messages_.size());
    const meshtastic_MqttClientProxyMessage &message = mockMeshService->messages_.front();
    TEST_ASSERT_EQUAL_STRING(MockPubSubServer::kTextTopic, message.topic);
    TEST_ASSERT_EQUAL(meshtastic_MqttClientProxyMessage_text_tag, message.which_payload_variant);
    TEST_ASSERT_EQUAL_STRING("payload", message.payload_variant.text);
}

// Helper method to verify the expected latitude/longitude was received.
void verifyLatLong(const DecodedServiceEnvelope &env, uint32_t latitude, uint32_t longitude)
{
    TEST_ASSERT_TRUE(env.validDecode);
    const meshtastic_MeshPacket &p = *env.packet;
    TEST_ASSERT_EQUAL(NODENUM_BROADCAST, p.to);
    TEST_ASSERT_EQUAL(meshtastic_MeshPacket_decoded_tag, p.which_payload_variant);
    TEST_ASSERT_EQUAL(meshtastic_PortNum_MAP_REPORT_APP, p.decoded.portnum);

    meshtastic_MapReport mapReport;
    TEST_ASSERT_TRUE(
        pb_decode_from_bytes(p.decoded.payload.bytes, p.decoded.payload.size, &meshtastic_MapReport_msg, &mapReport));
    TEST_ASSERT_EQUAL(latitude, mapReport.latitude_i);
    TEST_ASSERT_EQUAL(longitude, mapReport.longitude_i);
}

// Map reporting defaults to an imprecise location.
void test_reportToMapDefaultImprecise(void)
{
    unitTest->reportToMap();

    TEST_ASSERT_EQUAL(1, pubsub->published_.size());
    const auto &[topic, payload] = pubsub->published_.front();
    TEST_ASSERT_EQUAL_STRING("msh/2/map/", topic.c_str());
}

// Location is sent over the phone proxy.
void test_reportToMapImpreciseProxied(void)
{
    moduleConfig.mqtt.proxy_to_client_enabled = true;
    MQTTUnitTest::restart();

    unitTest->reportToMap(/*precision=*/14);

    TEST_ASSERT_EQUAL(1, mockMeshService->messages_.size());
    const meshtastic_MqttClientProxyMessage &message = mockMeshService->messages_.front();
    TEST_ASSERT_EQUAL_STRING("msh/2/map/", message.topic);
    TEST_ASSERT_EQUAL(meshtastic_MqttClientProxyMessage_data_tag, message.which_payload_variant);
    const DecodedServiceEnvelope env(message.payload_variant.data.bytes, message.payload_variant.data.size);
}

// isUsingDefaultServer returns true when using the default server.
void test_usingDefaultServer(void)
{
    TEST_ASSERT_TRUE(mqtt->isUsingDefaultServer());
}

// isUsingDefaultServer returns true when using the default server and a port.
void test_usingDefaultServerWithPort(void)
{
    std::string server = default_mqtt_address;
    server += ":1883";
    strcpy(moduleConfig.mqtt.address, server.c_str());
    MQTTUnitTest::restart();

    TEST_ASSERT_TRUE(mqtt->isUsingDefaultServer());
}

// isUsingDefaultServer returns true when using the default server and invalid port.
void test_usingDefaultServerWithInvalidPort(void)
{
    std::string server = default_mqtt_address;
    server += ":invalid";
    strcpy(moduleConfig.mqtt.address, server.c_str());
    MQTTUnitTest::restart();

    TEST_ASSERT_TRUE(mqtt->isUsingDefaultServer());
}

// isUsingDefaultServer returns false when not using the default server.
void test_usingCustomServer(void)
{
    strcpy(moduleConfig.mqtt.address, "custom");
    MQTTUnitTest::restart();

    TEST_ASSERT_FALSE(mqtt->isUsingDefaultServer());
}

// Test that isEnabled returns true the MQTT module is enabled.
void test_enabled(void)
{
    TEST_ASSERT_TRUE(mqtt->isEnabled());
}

// Test that isEnabled returns false the MQTT module not enabled.
void test_disabled(void)
{
    moduleConfig.mqtt.enabled = false;
    MQTTUnitTest::restart();

    TEST_ASSERT_FALSE(mqtt->isEnabled());
}

// Subscriptions contain the moduleConfig.mqtt.root prefix.
void test_customMqttRoot(void)
{
    strcpy(moduleConfig.mqtt.root, "custom");
    MQTTUnitTest::restart();

    TEST_ASSERT_TRUE(loopUntil(
        [] { return pubsub->subscriptions_.count("custom/2/e/test/+") && pubsub->subscriptions_.count("custom/2/e/PKI/+"); }));
}

// Empty configuration is valid.
void test_configEmptyIsValid(void)
{
    meshtastic_ModuleConfig_MQTTConfig config = {};

    TEST_ASSERT_TRUE(MQTT::isValidConfig(config));
}

// Empty 'enabled' configuration is valid.
void test_configEnabledEmptyIsValid(void)
{
    meshtastic_ModuleConfig_MQTTConfig config = {.enabled = true};
    MockPubSubServer client;

    TEST_ASSERT_TRUE(MQTTUnitTest::isValidConfig(config, &client));
    TEST_ASSERT_TRUE(client.connected_);
    TEST_ASSERT_EQUAL_STRING(default_mqtt_address, client.host_.c_str());
    TEST_ASSERT_EQUAL(1883, client.port_);
}

// Configuration with the default server is valid.
void test_configWithDefaultServer(void)
{
    meshtastic_ModuleConfig_MQTTConfig config = {.address = default_mqtt_address};

    TEST_ASSERT_TRUE(MQTT::isValidConfig(config));
}

// Configuration with the default server and port 8888 is invalid.
void test_configWithDefaultServerAndInvalidPort(void)
{
    meshtastic_ModuleConfig_MQTTConfig config = {.address = default_mqtt_address ":8888"};

    TEST_ASSERT_FALSE(MQTT::isValidConfig(config));
}

// Configuration with the default server and tls_enabled = true is invalid.
void test_configWithDefaultServerAndInvalidTLSEnabled(void)
{
    meshtastic_ModuleConfig_MQTTConfig config = {.tls_enabled = true};

    TEST_ASSERT_FALSE(MQTT::isValidConfig(config));
}

// isValidConfig connects to a custom host and port.
void test_configCustomHostAndPort(void)
{
    meshtastic_ModuleConfig_MQTTConfig config = {.enabled = true, .address = "server:1234"};
    MockPubSubServer client;

    TEST_ASSERT_TRUE(MQTTUnitTest::isValidConfig(config, &client));
    TEST_ASSERT_TRUE(client.connected_);
    TEST_ASSERT_EQUAL_STRING("server", client.host_.c_str());
    TEST_ASSERT_EQUAL(1234, client.port_);
}

// isValidConfig returns false if a connection cannot be established.
void test_configWithConnectionFailure(void)
{
    meshtastic_ModuleConfig_MQTTConfig config = {.enabled = true, .address = "server"};
    MockPubSubServer client;
    client.refuseConnection_ = true;

    TEST_ASSERT_FALSE(MQTTUnitTest::isValidConfig(config, &client));
}

// isValidConfig returns true when tls_enabled is supported, or false otherwise.
void test_configWithTLSEnabled(void)
{
    meshtastic_ModuleConfig_MQTTConfig config = {.enabled = true, .address = "server", .tls_enabled = true};
    MockPubSubServer client;

#if MQTT_SUPPORTS_TLS
    TEST_ASSERT_TRUE(MQTTUnitTest::isValidConfig(config, &client));
#else
    TEST_ASSERT_FALSE(MQTTUnitTest::isValidConfig(config, &client));
#endif
}

void setup()
{
    initializeTestEnvironment();
    const std::unique_ptr<MockNodeDB> mockNodeDB(new MockNodeDB());
    nodeDB = mockNodeDB.get();

    UNITY_BEGIN();
    RUN_TEST(test_sendDirectlyConnectedDecoded);
    RUN_TEST(test_sendDirectlyConnectedEncrypted);
    RUN_TEST(test_proxyToMeshServiceDecoded);
    RUN_TEST(test_proxyToMeshServiceEncrypted);
    RUN_TEST(test_dontMqttMeOnPublicServer);
    RUN_TEST(test_okToMqttOnPrivateServer);
    RUN_TEST(test_noRangeTestAppOnDefaultServer);
    RUN_TEST(test_noDetectionSensorAppOnDefaultServer);
    RUN_TEST(test_sendQueued);
    RUN_TEST(test_reconnectProxyDoesNotReconnectMqtt);
    RUN_TEST(test_receiveEmptyMeshPacket);
    RUN_TEST(test_receiveDecodedProto);
    RUN_TEST(test_receiveDecodedProtoFromProxy);
    RUN_TEST(test_receiveEmptyDataFromProxy);
    RUN_TEST(test_receiveWithoutChannelDownlink);
    RUN_TEST(test_receiveEncryptedPKITopicToUs);
    RUN_TEST(test_receiveIgnoresOwnPublishedMessages);
    RUN_TEST(test_receiveAcksOwnSentMessages);
    RUN_TEST(test_receiveIgnoresSentMessagesFromOthers);
    RUN_TEST(test_receiveIgnoresDecodedWhenEncryptionEnabled);
    RUN_TEST(test_receiveIgnoresDecodedAdminApp);
    RUN_TEST(test_receiveIgnoresUnexpectedFields);
    RUN_TEST(test_receiveIgnoresInvalidHopLimit);
    RUN_TEST(test_publishTextMessageDirect);
    RUN_TEST(test_publishTextMessageWithProxy);
    RUN_TEST(test_reportToMapDefaultImprecise);
    RUN_TEST(test_reportToMapImpreciseProxied);
    RUN_TEST(test_usingDefaultServer);
    RUN_TEST(test_usingDefaultServerWithPort);
    RUN_TEST(test_usingDefaultServerWithInvalidPort);
    RUN_TEST(test_usingCustomServer);
    RUN_TEST(test_enabled);
    RUN_TEST(test_disabled);
    RUN_TEST(test_customMqttRoot);
    RUN_TEST(test_configEmptyIsValid);
    RUN_TEST(test_configEnabledEmptyIsValid);
    RUN_TEST(test_configWithDefaultServer);
    RUN_TEST(test_configWithDefaultServerAndInvalidPort);
    RUN_TEST(test_configWithDefaultServerAndInvalidTLSEnabled);
    RUN_TEST(test_configCustomHostAndPort);
    RUN_TEST(test_configWithConnectionFailure);
    RUN_TEST(test_configWithTLSEnabled);
    exit(UNITY_END());
}
#else
void setup()
{
    initializeTestEnvironment();
    LOG_WARN("This test requires the ARCH_PORTDUINO variant of WiFiClient");
    UNITY_BEGIN();
    UNITY_END();
}
#endif
void loop() {}