#include "TunnelPlugin.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "RTC.h"
#include "Router.h"
#include "configuration.h"
#include "main.h"
#include <Arduino.h>
#include <assert.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <HTTPClient.h>
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

#define RXD2 35
#define TXD2 15
#define SERIALPLUGIN_RX_BUFFER 128
#define SERIALPLUGIN_STRING_MAX Constants_DATA_PAYLOAD_LEN
#define SERIALPLUGIN_TIMEOUT 250
#define SERIALPLUGIN_BAUD 9600
#define SERIALPLUGIN_ACK 1


#ifdef HAS_EINK
// The screen is bigger so use bigger fonts
#define FONT_SMALL ArialMT_Plain_16
#define FONT_MEDIUM ArialMT_Plain_24
#define FONT_LARGE ArialMT_Plain_24
#else
#define FONT_SMALL ArialMT_Plain_10
#define FONT_MEDIUM ArialMT_Plain_16
#define FONT_LARGE ArialMT_Plain_24
#endif

#define fontHeight(font) ((font)[1] + 1) // height is position 1

#define FONT_HEIGHT_SMALL fontHeight(FONT_SMALL)
#define FONT_HEIGHT_MEDIUM fontHeight(FONT_MEDIUM)

char tunnelSerialStringChar[Constants_DATA_PAYLOAD_LEN];

int32_t TunnelPlugin::runOnce()
{
#ifndef NO_ESP32
    radioConfig.preferences.tunnelplugin_enabled = 1;
    radioConfig.preferences.tunnelplugin_echo_enabled = 1;

    if (radioConfig.preferences.tunnelplugin_enabled) {

        if (firstTime) {
            DEBUG_MSG("Initializing Serial 2\n");
            Serial2.begin(SERIALPLUGIN_BAUD, SERIAL_8N1, RXD2, TXD2);
            Serial2.setTimeout(SERIALPLUGIN_TIMEOUT);
            Serial2.setRxBufferSize(SERIALPLUGIN_RX_BUFFER);
            firstTime = 0;

        } else {
            String serialString;
            while (Serial2.available()) {
                DEBUG_MSG("Serial Has Data\n");
                serialString = Serial2.readString();
                serialString.toCharArray(tunnelSerialStringChar, Constants_DATA_PAYLOAD_LEN);
                DEBUG_MSG("Recevied: %s\n", tunnelSerialStringChar);
                sendPayload(tunnelSerialStringChar);

            }
            DEBUG_MSG("\n");
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

MeshPacket *TunnelPlugin::allocReply(char* tagId)
{

    NodeInfo *node = service.refreshMyNodeInfo(); // should guarantee there is now a position
    TagSightingMessage m = TagSightingMessage_init_default;
    
    memcpy(&m.tagId, tagId, 16);

    if(node->has_position){
        Position p = node->position;

        m.latitude_i = p.latitude_i;
        m.longitude_i = p.longitude_i;
        m.time = p.time;
        // if (getRTCQuality() < RTCQualityGPS) {
        //     DEBUG_MSG("Tunnel node does not have a time! Will send a null time.\n");
        //     p.time = 0;
        // } else
        //     DEBUG_MSG("Tunnel node has time. Adding to Sighting Msg: %u\n", p.time);
        //     m.time = p.time;
    }

    return allocDataProtobuf(m);
}

void TunnelPlugin::sendPayload(char* tagId, NodeNum dest, bool wantReplies)
{
    MeshPacket *p = allocReply(tagId);
    p->to = dest;
    p->decoded.want_response = wantReplies;
    service.sendToMesh(p);
}

bool TunnelPlugin::handleReceivedProtobuf(const MeshPacket &mp, const TagSightingMessage *pptr)
{
    lastSightingPacket = packetPool.allocCopy(mp);

#ifndef NO_ESP32
    auto p = *pptr;
    DEBUG_MSG("Received Tag tagId:%s time:%u\n", pptr -> tagId, p.time);

    if (radioConfig.preferences.tunnelplugin_enabled && WiFi.status() == WL_CONNECTED) {
        if (getFrom(&mp) != nodeDB.getNodeNum() || mp.to == NODENUM_BROADCAST) {
            DEBUG_MSG("Sending To Server\n");
            HTTPClient https;
            char requestUrl [256];
            snprintf( requestUrl,  sizeof(requestUrl), url, 
                p.tagId,
                getFrom(&mp),
                p.time,
                p.latitude_i,
                p.longitude_i
            );
            // DEBUG_MSG("TUNNEL SIZE BEFORE: %d \n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
            if (https.begin(requestUrl)) {
                https.addHeader("Content-Type", "application/json");
                https.addHeader("Content-Length", "0");
                
                DEBUG_MSG("[HTTPS] Post:");
                int httpCode = https.POST("");

                if (httpCode > 0) {
                    // HTTP header has been send and Server response header has been handled
                    DEBUG_MSG(" Success (%d)\n", httpCode);
                } else {
                    DEBUG_MSG(" Failed (%d, %s)\n", https.errorToString(httpCode).c_str(), httpCode);
                }
                https.end();
            }
            
        }
    } else {
        DEBUG_MSG("Tunnel Plugin Disabled\n");
    }

#endif
    return true; // Let others look at this message also if they want
}

void TunnelPlugin::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);
    display->drawString(x, y, "Sightings");
    if (lastSightingPacket == nullptr) {
        display->setFont(FONT_SMALL);
        display->drawString(x, y += fontHeight(FONT_MEDIUM), "No Sightings Yet");
        return;
    }

    TagSightingMessage lastMeasurement;
    
    auto &p = lastSightingPacket->decoded;
    if (!pb_decode_from_bytes(p.payload.bytes, 
            p.payload.size,
            TagSightingMessage_fields, 
            &lastMeasurement)) {
        display->setFont(FONT_SMALL);
        display->drawString(x, y += fontHeight(FONT_MEDIUM), "Parse Error");
        return;
    }

    long hms = lastMeasurement.time % SEC_PER_DAY;
    
    hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

    int hour = hms / SEC_PER_HOUR;
    int min = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
    int sec = (hms % SEC_PER_HOUR) % SEC_PER_MIN;

    char timebuf[9];
    snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d", hour, min, sec);

    display->setFont(FONT_SMALL);

    char idString[25];
    snprintf(idString, sizeof(idString), "Id:%s", lastMeasurement.tagId);
    display -> drawString(x, y += fontHeight(FONT_MEDIUM), idString);

    char timeString[25];
    snprintf(timeString, sizeof(timeString), "Time:%s", timebuf);
    display -> drawString(x, y += fontHeight(FONT_SMALL), timeString);
}
