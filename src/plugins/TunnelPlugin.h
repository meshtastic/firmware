#pragma once

#include "SinglePortPlugin.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Arduino.h>
#include <functional>

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

class TunnelPlugin : private concurrency::OSThread
{
    bool firstTime = 1;

  public:
    TunnelPlugin();

  protected:
    virtual int32_t runOnce();
};

extern TunnelPlugin *tunnelPlugin;

/*
 * Radio interface for SerialPlugin
 *
 */
class TunnelPluginRadio : public SinglePortPlugin
{
    uint32_t lastRxID;

  public:
    /*
        TODO: Switch this to PortNum_SERIAL_APP once the change is able to be merged back here
              from the main code.
    */

    TunnelPluginRadio();

    /**
     * Send our payload into the mesh
     */
    void sendPayload(NodeNum dest = NODENUM_BROADCAST, bool wantReplies = false);
  
    #ifndef NO_SCREEN
      virtual void drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y) { 
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->setFont(FONT_SMALL);
        display->drawString(64 + x, y, "Tunnel");
       }
    #endif
  
  protected:
    virtual MeshPacket *allocReply();

    /** Called to handle a particular incoming message

    @return true if you've guaranteed you've handled this message and no other handlers should be considered for it
    */
    virtual bool handleReceived(const MeshPacket &mp);

    virtual bool wantUIFrame() { return true; }

};

extern TunnelPluginRadio *tunnelPluginRadio;
