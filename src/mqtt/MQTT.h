#pragma once

#include "configuration.h"

#include "concurrency/OSThread.h"
#include "mesh/Channels.h"
#include "mesh/generated/meshtastic/mqtt.pb.h"
#include <PubSubClient.h>
#if HAS_WIFI
#include <WiFiClient.h>
#if !defined(ARCH_PORTDUINO)
#include <WiFiClientSecure.h>
#endif
#endif
#if HAS_ETHERNET
#include <EthernetClient.h>
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
#if !defined(DEBUG_HEAP_MQTT)
    PubSubClient pubSub;

  public:
#else
  public:
    PubSubClient pubSub;
#endif
    MQTT();

    /**
     * Publish a packet on the glboal MQTT server.
     * This hook must be called **after** the packet is encrypted (including the channel being changed to a hash).
     * @param chIndex the index of the channel for this message
     *
     * Note: for messages we are forwarding on the mesh that we can't find the channel for (because we don't have the keys), we
     * can not forward those messages to the cloud - becuase no way to find a global channel ID.
     */
    void onSend(const meshtastic_MeshPacket &mp, ChannelIndex chIndex);

    /** Attempt to connect to server if necessary
     */
    void reconnect();

    bool connected();

  protected:
    PointerQueue<meshtastic_ServiceEnvelope> mqttQueue;

    int reconnectCount = 0;

    virtual int32_t runOnce() override;

  private:
    /** return true if we have a channel that wants uplink/downlink
     */
    bool wantsLink() const;

    /** Tell the server what subscriptions we want (based on channels.downlink_enabled)
     */
    void sendSubscriptions();

    /// Just C glue to call onPublish
    static void mqttCallback(char *topic, byte *payload, unsigned int length);

    /// Called when a new publish arrives from the MQTT server
    void onPublish(char *topic, byte *payload, unsigned int length);

    /// Called when a new publish arrives from the MQTT server
    std::string downstreamPacketToJson(meshtastic_MeshPacket *mp);

    /// Return 0 if sleep is okay, veto sleep if we are connected to pubsub server
    // int preflightSleepCb(void *unused = NULL) { return pubSub.connected() ? 1 : 0; }
};

void mqttInit();

extern MQTT *mqtt;