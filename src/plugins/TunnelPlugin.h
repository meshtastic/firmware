#pragma once
#include "ProtobufPlugin.h"
#include "mesh/generated/tag_sighting.pb.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>
#include <WiFiClientSecure.h>

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

class TunnelPlugin : public ProtobufPlugin<TagSightingMessage>, private concurrency::OSThread
{
    bool firstTime = 1;

  public:
    TunnelPlugin(): ProtobufPlugin("tunnelplugin", PortNum_TUNNEL_APP, TagSightingMessage_fields), concurrency::OSThread(
                                                                                                 "TunnelPlugin")
    {
      lastSightingPacket = nullptr;
    }

    void sendPayload(char* tagId, NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);
    virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
  
  protected:
    const char* url = "http://wildlife-server.azurewebsites.net/api/Devices/AnimalSighted?TagId=%s&TrackerId=%u&SightingTime=%u&Latitude=%d&Longitude=%d";
  
    virtual MeshPacket *allocReply(char* tagId);
    virtual bool handleReceivedProtobuf(const MeshPacket &mp, const TagSightingMessage *pptr);
    virtual bool wantUIFrame() { return true; }
    virtual int32_t runOnce();

    const MeshPacket *lastSightingPacket;
};

extern TunnelPlugin tunnelPlugin;
