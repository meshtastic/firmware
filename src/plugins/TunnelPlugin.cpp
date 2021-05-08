#include "TunnelPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include <Arduino.h>

#include <assert.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#define RXD2 16
#define TXD2 17
#define SERIALPLUGIN_RX_BUFFER 128
#define SERIALPLUGIN_STRING_MAX Constants_DATA_PAYLOAD_LEN
#define SERIALPLUGIN_TIMEOUT 250
#define SERIALPLUGIN_BAUD 38400
#define SERIALPLUGIN_ACK 1

TunnelPlugin *tunnelPlugin;
TunnelPluginRadio *tunnelPluginRadio;

TunnelPlugin::TunnelPlugin() : concurrency::OSThread("TunnelPlugin") {}

char tunnelSerialStringChar[Constants_DATA_PAYLOAD_LEN];

TunnelPluginRadio::TunnelPluginRadio() : SinglePortPlugin("TunnelPluginRadio", PortNum_TUNNEL_APP)
{
    boundChannel = Channels::serialChannel;
}

int32_t TunnelPlugin::runOnce()
{
#ifndef NO_ESP32

    radioConfig.preferences.tunnelplugin_enabled = 1;
    radioConfig.preferences.tunnelplugin_echo_enabled = 1;

    if (radioConfig.preferences.tunnelplugin_enabled) {

        if (firstTime) {
            DEBUG_MSG("Initializing tunnel serial peripheral interface\n");
            Serial1.begin(SERIALPLUGIN_BAUD, SERIAL_8N1, RXD2, TXD2);
            Serial1.setTimeout(SERIALPLUGIN_TIMEOUT);
            Serial1.setRxBufferSize(SERIALPLUGIN_RX_BUFFER);

            tunnelPluginRadio = new TunnelPluginRadio();

            firstTime = 0;

        } else {
            String serialString;

            while (Serial1.available()) {
                serialString = Serial1.readString();
                serialString.toCharArray(tunnelSerialStringChar, Constants_DATA_PAYLOAD_LEN);

                tunnelPluginRadio->sendPayload();

                DEBUG_MSG("Tunnel Reading Recevied: %s\n", tunnelSerialStringChar);
            }
        }

        return (1000);
    } else {
        DEBUG_MSG("Tunnel Plugin Disabled\n");

        return (INT32_MAX);
    }
#else
    return INT32_MAX;`
#endif
}

MeshPacket *TunnelPluginRadio::allocReply()
{

    auto reply = allocDataPacket(); // Allocate a packet for sending

    return reply;
}

void TunnelPluginRadio::sendPayload(NodeNum dest, bool wantReplies)
{
    MeshPacket *p = allocReply();
    p->to = dest;
    p->decoded.want_response = wantReplies;

    p->want_ack = SERIALPLUGIN_ACK;

    p->decoded.payload.size = strlen(tunnelSerialStringChar); // You must specify how many bytes are in the reply
    memcpy(p->decoded.payload.bytes, tunnelSerialStringChar, p->decoded.payload.size);

    service.sendToMesh(p);
}

bool TunnelPluginRadio::handleReceived(const MeshPacket &mp)
{
#ifndef NO_ESP32

    if (radioConfig.preferences.tunnelplugin_enabled && WiFi.status() == WL_CONNECTED) {

        auto &p = mp.decoded;
        if (getFrom(&mp) == nodeDB.getNodeNum()) {

            char *tag_id_str = "TagId=";
            char *tag_id = "1";
            char *tracker_id_str = "&TrackerId=";
            char *tracker_id = "1234";
            char *sight_time_str = "&SightingTime=";
            char *sight_time = "2021-05-07T08:01:50.557Z";

            char *query_str;  
            query_str =  (char*) malloc(
                strlen(tag_id_str) 
                + strlen(tag_id) + strlen(tracker_id_str) 
                + strlen(tracker_id) 
                + strlen(sight_time_str) 
                + strlen(sight_time)
            );

            strcpy(query_str, tag_id_str);
            strcat(query_str, tag_id);
            strcat(query_str, tracker_id_str);
            strcat(query_str, tracker_id);
            strcat(query_str, sight_time_str);
            strcat(query_str, sight_time);
            
            WiFiClientSecure *client = new WiFiClientSecure;
            if (client) {
                client -> setCACert(rootCACertificate);
                HTTPClient https;
                String url = "https://wildlife-server.azurewebsites.net/api/Sightings/AnimalSighted?TagId=1&SightingTime=2021-05-07T08:01:50.557Z&TrackerId=10";

                if (https.begin(*client, url)) {
                    https.addHeader("Content-Type", "application/json");
                    https.addHeader("Content-Length", "0");
                    int httpCode = https.POST("");

                    if (httpCode > 0) {
                        // HTTP header has been send and Server response header has been handled
                        DEBUG_MSG("[HTTPS] Post... code: %d\n", httpCode);

                        // file found at server
                        if (httpCode == HTTP_CODE_OK) {
                            String payload = https.getString();
                            DEBUG_MSG("[HTTPS] Post OK: %s", payload);
                        }
                    } else {
                        DEBUG_MSG("[HTTPS] Post... failed, error: %s, %i\n", https.errorToString(httpCode).c_str(), httpCode);
                    }
                }
            }
            
        }
    } else {
        DEBUG_MSG("Tunnel Plugin Disabled\n");
    }

#endif
    return true; // Let others look at this message also if they want
}
