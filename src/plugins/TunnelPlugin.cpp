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
                DEBUG_MSG("Tunnel Reading Recevied: %s\n", tunnelSerialStringChar);
                tunnelPluginRadio->sendPayload(NODENUM_BROADCAST, false, tunnelSerialStringChar);

            }
        }

        return (1000);
    } else {
        DEBUG_MSG("Tunnel Plugin Disabled\n");

        return (INT32_MAX);
    }
#else
    return INT32_MAX;
#endif
}

MeshPacket *TunnelPluginRadio::allocReply(char* tagId)
{

    NodeInfo *node = service.refreshMyNodeInfo(); // should guarantee there is now a position
    User *user = node->user;
    TagSightingMessage m = TagSightingMessage_init_default;
    
    m.tagId = tagId

    if(node->has_position){
        Position p = node->position;

        m.latitude_i = p.latitude_i;
        m.longitude_i = p.longitude_i;

        if (getRTCQuality() < RTCQualityGPS) {
            DEBUG_MSG("Tunnel node does not have a time! Will send a null time.\n");
            p.time = 0;
        } else
            DEBUG_MSG("Tunnel node has time. Adding to Sighting Msg: %u\n", p.time);
            m.time = p.time;
    }

    return allocDataProtobuf(p);
    return reply;
}

void TunnelPluginRadio::sendPayload(NodeNum dest, bool wantReplies, char* tagId)
{
    MeshPacket *p = allocReply(tagId);
    p->to = dest;
    p->decoded.want_response = wantReplies;
    service.sendToMesh(p);
}

bool TunnelPluginRadio::handleReceived(const MeshPacket &mp, const TagSightingMessage *pptr)
{
#ifndef NO_ESP32
    auto p = *pptr;
    DEBUG_MSG("Received Tag sighting=%u\n", p.time);

    if (radioConfig.preferences.tunnelplugin_enabled && WiFi.status() == WL_CONNECTED) {
        DEBUG_MSG("Sending To Server\n");

        if (getFrom(&mp) == nodeDB.getNodeNum() || mp.to == NODENUM_BROADCAST) {
            
            char* baseServerUrl = "https://wildlife-server.azurewebsites.net/api/Sightings/AnimalSighted?";
            char *tag_id_str = "TagId=";
            char *tag_id = "1";
            char *tracker_id_str = "&TrackerId=";
            char *tracker_id = "1234";
            char *sight_time_str = "&SightingTime=";
            char *sight_time = "2021-05-07T08:01:50.557Z";

            char *url;  
            url =  (char*) malloc(
                strlen(baseServerUrl)
                + strlen(tag_id_str) 
                + strlen(tag_id) 
                + strlen(tracker_id_str) 
                + strlen(tracker_id) 
                + strlen(sight_time_str) 
                + strlen(sight_time)
            );

            strcpy(url, baseServerUrl);
            strcat(url, tag_id_str);
            strcat(url, tag_id);
            strcat(url, tracker_id_str);
            strcat(url, tracker_id);
            strcat(url, sight_time_str);
            strcat(url, sight_time);
            
            DEBUG_MSG("Uri %s\n", url);

            WiFiClientSecure *client = new WiFiClientSecure;
            if (client) {
                client -> setCACert(rootCACertificate);
                HTTPClient https;

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
                        else{
                            DEBUG_MSG("Sending To Server FAILED. Not OK\n");
                        }
                    } else {
                        DEBUG_MSG("[HTTPS] Post... failed, error: %s, %i\n", https.errorToString(httpCode).c_str(), httpCode);
                    }
                }
            }else{
                DEBUG_MSG("Sending To Server FAILED. No Client\n");
            }
            
        }
        DEBUG_MSG("Sending To Server DONE\n");
    } else {
        DEBUG_MSG("Tunnel Plugin Disabled\n");
    }

#endif
    return true; // Let others look at this message also if they want
}
