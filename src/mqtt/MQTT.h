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
    // supposedly the current version is busted:
    // http://www.iotsharing.com/2017/08/how-to-use-esp32-mqtts-with-mqtts-mosquitto-broker-tls-ssl.html
    // WiFiClientSecure wifiClient;
    WiFiClient mqttClient;
    PubSubClient pubSub;

  public:
    MQTT();

    /**
     * Publish a packet on the glboal MQTT server.
     */
    void publish(const MeshPacket &mp);

  private:
    const char *getCryptTopic(const char *channelId);
};

void mqttInit();

extern MQTT *mqtt;
