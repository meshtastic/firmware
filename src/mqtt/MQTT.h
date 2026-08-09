#pragma once

#include "Default.h"
#include "configuration.h"

#include "concurrency/OSThread.h"
#include "mesh/Channels.h"
#include "mesh/generated/meshtastic/mqtt.pb.h"
#if HAS_WIFI
#include <WiFiClient.h>
#if __has_include(<WiFiClientSecure.h>)
#include <WiFiClientSecure.h>
#endif
#endif
#if HAS_ETHERNET && !defined(USE_WS5500) && !defined(USE_CH390D)
#include <EthernetClient.h>
#endif
#if HAS_CELLULAR
#include "mesh/cell/CellClient.h"
#endif

#if HAS_NETWORKING
#include <PubSubClient.h>
#include <memory>
#endif

#define MAX_MQTT_QUEUE 16

/**
 * Our wrapper/singleton for sending/receiving MQTT "udp" packets.  This object isolates the MQTT protocol implementation from
 * the two components that use it: MQTTPlugin and MQTTSimInterface.
 */
class MQTT : private concurrency::OSThread
{
  public:
    MQTT();

    /**
     * Publish a packet on the global MQTT server.
     * @param mp_encrypted the encrypted packet to publish
     * @param mp_decoded the decrypted packet to publish
     * @param chIndex the index of the channel for this message
     *
     * Note: for messages we are forwarding on the mesh that we can't find the channel for (because we don't have the keys), we
     * can not forward those messages to the cloud - because no way to find a global channel ID.
     */
    void onSend(const meshtastic_MeshPacket &mp_encrypted, const meshtastic_MeshPacket &mp_decoded, ChannelIndex chIndex);

    bool isConnectedDirectly();

    bool publish(const char *topic, const char *payload, bool retained);

    bool publish(const char *topic, const uint8_t *payload, size_t length, const bool retained);

    void onClientProxyReceive(meshtastic_MqttClientProxyMessage msg);

    bool isEnabled() { return this->enabled; };

    void start() { setIntervalFromNow(0); };

    bool isUsingDefaultServer() { return isConfiguredForDefaultServer; }
    bool isUsingDefaultRootTopic() { return isConfiguredForDefaultRootTopic; }

    /// Validate the meshtastic_ModuleConfig_MQTTConfig.
    static bool isValidConfig(const meshtastic_ModuleConfig_MQTTConfig &config) { return isValidConfig(config, nullptr); }

  protected:
    struct QueueEntry {
        std::string topic;
        std::basic_string<uint8_t> envBytes; // binary/pb_encode_to_bytes ServiceEnvelope
    };
    PointerQueue<QueueEntry> mqttQueue;

    int reconnectCount = 0;
    bool isConfiguredForDefaultServer = true;
    bool isConfiguredForDefaultRootTopic = true;

    virtual int32_t runOnce() override;

#ifndef PIO_UNIT_TESTING
  private:
#endif
// The primary transport's client type. Boards carrying a second link keep an
// extra client below and choose between them at connect time.
#if HAS_WIFI
    using MQTTClient = WiFiClient;
#if __has_include(<WiFiClientSecure.h>)
    using MQTTClientTLS = WiFiClientSecure;
#define MQTT_SUPPORTS_TLS 1
#endif
#elif HAS_ETHERNET
    using MQTTClient = EthernetClient;
#elif HAS_CELLULAR
    using MQTTClient = CellClient;
#else
    using MQTTClient = void;
#endif

// Cellular coexists with WiFi/Ethernet instead of replacing them, so it needs
// its own client whenever one of those is the primary transport.
#define MQTT_HAS_SECONDARY_CELL (HAS_CELLULAR && (HAS_WIFI || HAS_ETHERNET))

// Whether tls_enabled could be honored on some transport - WiFi's WiFiClientSecure,
// or cellular's AT+CIPSSL, checked live per connection rather than by this macro.
#define MQTT_TLS_POSSIBLE (MQTT_SUPPORTS_TLS || HAS_CELLULAR)

#if HAS_NETWORKING
    std::unique_ptr<MQTTClient> mqttClient;
#if MQTT_SUPPORTS_TLS
    MQTTClientTLS mqttClientTLS;
#endif
#if MQTT_HAS_SECONDARY_CELL
    CellClient mqttClientCell;
#endif
    PubSubClient pubSub;
    explicit MQTT(std::unique_ptr<MQTTClient> mqttClient);

    /// Client for whichever transport is up, or nullptr when none is.
    Client *activeClient();
#if HAS_CELLULAR
    /// The single cellular Client, whether it's the primary transport or a secondary alongside WiFi/Ethernet.
    CellClient *cellClient();
#endif
#endif

    std::string cryptTopic = "/2/e/"; // msh/2/e/CHANNELID/NODEID
    std::string mapTopic = "/2/map/"; // For protobuf-encoded MapReport messages

    // For map reporting (only applies when enabled)
    const uint32_t default_map_position_precision = 14; // defaults to max. offset of ~1459m
    uint32_t last_report_to_map = 0;
    uint32_t map_position_precision = default_map_position_precision;
    uint32_t map_publish_interval_msecs = default_map_publish_interval_secs * 1000;

    /** Attempt to connect to server if necessary
     */
    void reconnect();

    /** Tell the server what subscriptions we want (based on channels.downlink_enabled)
     */
    void sendSubscriptions();

    /// Callback for direct mqtt subscription messages
    static void mqttCallback(char *topic, byte *payload, unsigned int length);

    static bool isValidConfig(const meshtastic_ModuleConfig_MQTTConfig &config, MQTTClient *client);

    /// Called when a new publish arrives from the MQTT server
    void onReceive(char *topic, byte *payload, size_t length);

    void publishQueuedMessages();

    void publishNodeInfo();

    // Check if we should report unencrypted information about our node for consumption by a map
    void perhapsReportToMap();

    /// Return 0 if sleep is okay, veto sleep if we are connected to pubsub server
    // int preflightSleepCb(void *unused = NULL) { return pubSub.connected() ? 1 : 0; }
};

void mqttInit();

extern MQTT *mqtt;