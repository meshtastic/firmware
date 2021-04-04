#pragma once

#include "configuration.h"

#include "concurrency/OSThread.h"
#include "mesh/Channels.h"
#include <PubSubClient.h>
#include <WiFiClient.h>

/**
 * Our wrapper/singleton for sending/receiving MQTT "udp" packets.  This object isolates the MQTT protocol implementation from
 * the two components that use it: MQTTPlugin and MQTTSimInterface.
 */
class MQTT : private concurrency::OSThread
{
    // supposedly the current version is busted:
    // http://www.iotsharing.com/2017/08/how-to-use-esp32-mqtts-with-mqtts-mosquitto-broker-tls-ssl.html
    // WiFiClientSecure wifiClient;
    WiFiClient mqttClient;
    PubSubClient pubSub;

  public:
    MQTT();

    /**
     * Publish a packet on the glboal MQTT server.
     * This hook must be called **after** the packet is encrypted (including the channel being changed to a hash).
     * @param chIndex the index of the channel for this message
     *
     * Note: for messages we are forwarding on the mesh that we can't find the channel for (because we don't have the keys), we
     * can not forward those messages to the cloud - becuase no way to find a global channel ID.
     */
    void onSend(const MeshPacket &mp, ChannelIndex chIndex);

  protected:
    virtual int32_t runOnce();

  private:
    const char *getCryptTopic(const char *channelId);

    /** return true if we have a channel that wants uplink/downlink
     */
    bool wantsLink() const;

    /** Attempt to connect to server if necessary
     */
    void reconnect();
};

void mqttInit();

extern MQTT *mqtt;
