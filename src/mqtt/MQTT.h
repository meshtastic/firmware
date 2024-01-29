#pragma once

#include "configuration.h"

#include "concurrency/OSThread.h"
#include "mesh/Channels.h"
#include "mesh/generated/meshtastic/mqtt.pb.h"
#include "mqtt/JSON.h"
#if HAS_WIFI
#include <WiFiClient.h>
#define HAS_NETWORKING 1
#if !defined(ARCH_PORTDUINO)
#include <WiFiClientSecure.h>
#endif
#endif
#if HAS_ETHERNET
#include <EthernetClient.h>
#define HAS_NETWORKING 1
#endif

#ifdef HAS_NETWORKING
#include <PubSubClient.h>
#endif

#define MAX_MQTT_QUEUE 16

/**
 * Our wrapper/singleton for sending/receiving MQTT "udp" packets.  This object isolates the MQTT protocol implementation from
 * the two components that use it: MQTTPlugin and MQTTSimInterface.
 */
class MQTT : private concurrency::OSThread
{
    // supposedly the current version is busted:
    // http://www.iotsharing.com/2017/08/how-to-use-esp32-mqtts-with-mqtts-mosquitto-broker-tls-ssl.html
#if HAS_WIFI
    WiFiClient mqttClient;
#if !defined(ARCH_PORTDUINO)
    WiFiClientSecure wifiSecureClient;
#endif
#endif
#if HAS_ETHERNET
    EthernetClient mqttClient;
#endif

  public:
#ifdef HAS_NETWORKING
    PubSubClient pubSub;
#endif
    MQTT();

    /**
     * Publish a packet on the global MQTT server.
     * This hook must be called **after** the packet is encrypted (including the channel being changed to a hash).
     * @param chIndex the index of the channel for this message
     *
     * Note: for messages we are forwarding on the mesh that we can't find the channel for (because we don't have the keys), we
     * can not forward those messages to the cloud - because no way to find a global channel ID.
     */
    void onSend(const meshtastic_MeshPacket &mp, const meshtastic_MeshPacket &mp_decoded, ChannelIndex chIndex);

    /** Attempt to connect to server if necessary
     */
    void reconnect();

    bool isConnectedDirectly();

    bool publish(const char *topic, const char *payload, bool retained);

    bool publish(const char *topic, const uint8_t *payload, size_t length, const bool retained);

    void onClientProxyReceive(meshtastic_MqttClientProxyMessage msg);

  protected:
    PointerQueue<meshtastic_ServiceEnvelope> mqttQueue;

    int reconnectCount = 0;

    virtual int32_t runOnce() override;

  private:
    std::string statusTopic = "/2/stat/";
    std::string cryptTopic = "/2/c/";   // msh/2/c/CHANNELID/NODEID
    std::string jsonTopic = "/2/json/"; // msh/2/json/CHANNELID/NODEID
    /** return true if we have a channel that wants uplink/downlink
     */
    bool wantsLink() const;

    /** Tell the server what subscriptions we want (based on channels.downlink_enabled)
     */
    void sendSubscriptions();

    /// Callback for direct mqtt subscription messages
    static void mqttCallback(char *topic, byte *payload, unsigned int length);

    /// Called when a new publish arrives from the MQTT server
    void onReceive(char *topic, byte *payload, size_t length);

    /// Called when a new publish arrives from the MQTT server
    std::string meshPacketToJson(meshtastic_MeshPacket *mp);

    void publishStatus();
    void publishQueuedMessages();

    // returns true if this is a valid JSON envelope which we accept on downlink
    bool isValidJsonEnvelope(JSONObject &json);

    /// Return 0 if sleep is okay, veto sleep if we are connected to pubsub server
    // int preflightSleepCb(void *unused = NULL) { return pubSub.connected() ? 1 : 0; }
};

void mqttInit();

extern MQTT *mqtt;