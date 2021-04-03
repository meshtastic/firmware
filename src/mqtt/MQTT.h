#pragma once

#include "configuration.h"

#include <PubSubClient.h>
#include <WiFiClient.h>

/**
 * Our wrapper/singleton for sending/receiving MQTT "udp" packets.  This object isolates the MQTT protocol implementation from
 * the two components that use it: MQTTPlugin and MQTTSimInterface.
 */
class MQTT
{
    /// Our globally unique node ID
    String nodeId = "fixmemode";

    // supposedly the current version is busted:
    // http://www.iotsharing.com/2017/08/how-to-use-esp32-mqtts-with-mqtts-mosquitto-broker-tls-ssl.html
    // WiFiClientSecure wifiClient;
    WiFiClient mqttClient;
    PubSubClient pubSub;

  public:
    MQTT();

    /**
     * Publish a packet on the glboal MQTT server.
     * @param channelId must be a globally unique channel ID
     */
    void publish(const MeshPacket *mp, String channelId);

  private:
    const char *getTopic(String suffix, const char *direction = "dev");
};

void mqttInit();

extern MQTT *mqtt;
